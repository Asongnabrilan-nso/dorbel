#include <Arduino.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "camera_pins.h"
#include "intercom.h"

#if defined(LED_GPIO_NUM)
// Camera XCLK already uses LEDC_CHANNEL_0, so the flash LED needs its own
// channel. This project targets arduino-esp32 core 2.x, whose LEDC API is
// ledcSetup()+ledcAttachPin() rather than core 3.x's single-call ledcAttach().
#define LED_LEDC_CHANNEL 1
void setupLedFlash(int pin) {
  ledcSetup(LED_LEDC_CHANNEL, 5000, 8);
  ledcAttachPin(pin, LED_LEDC_CHANNEL);
}
#endif

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char INDEX_HTML[] =
    "<!DOCTYPE html><html><head><title>XIAO ESP32S3 Camera</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>"
    "body{margin:0;background:#111;display:flex;justify-content:center;"
    "align-items:center;height:100vh}"
    "img{max-width:100%;height:auto;display:block}"
    "</style></head><body><img src=\"/stream\"></body></html>";

// APP_MODE_DORBEL page: video comes from the stream server on port 81,
// because a stream occupies its server's only worker task for as long as
// it runs.
static const char DORBEL_INDEX_HTML[] =
    "<!DOCTYPE html><html><head><title>Dorbel camera</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>"
    "body{margin:0;background:#111;display:flex;justify-content:center;"
    "align-items:center;height:100vh}"
    "img{max-width:100%;height:auto;display:block}"
    "</style></head><body><img id=\"v\">"
    "<script>v.src='http://'+location.hostname+':81/stream'</script>"
    "</body></html>";

static httpd_handle_t stream_httpd = NULL;
static httpd_handle_t control_httpd = NULL;

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t dorbel_index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, DORBEL_INDEX_HTML, strlen(DORBEL_INDEX_HTML));
}

// Resolution of the /capture photo. The live stream stays small for frame
// rate; the photo is for recognising the visitor. UXGA (1600x1200) is the
// largest the frame buffers were allocated for in configureCamera().
#define CAPTURE_FRAMESIZE FRAMESIZE_UXGA

// One JPEG, used by the UNO Q for the Telegram alert photo. Switches the
// sensor to CAPTURE_FRAMESIZE for this one shot, then back to the stream size.
static esp_err_t capture_handler(httpd_req_t *req) {
  sensor_t *sensor = esp_camera_sensor_get();
  framesize_t streamSize = sensor->status.framesize;
  sensor->set_framesize(sensor, CAPTURE_FRAMESIZE);

  // Frames already queued are still the old size, and the first frame after
  // the switch can be badly exposed - skip those and keep the second big one.
  camera_fb_t *fb = NULL;
  int bigFrames = 0;
  for (int i = 0; i < 8; i++) {
    fb = esp_camera_fb_get();
    if (!fb) {
      break;
    }
    if (fb->width == resolution[CAPTURE_FRAMESIZE].width && ++bigFrames >= 2) {
      break;
    }
    esp_camera_fb_return(fb);
    fb = NULL;
  }

  if (!fb) {
    sensor->set_framesize(sensor, streamSize);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  sensor->set_framesize(sensor, streamSize);
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res;
  char part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else if (fb->format != PIXFORMAT_JPEG) {
      Serial.println("Non-JPEG frame skipped");
      esp_camera_fb_return(fb);
      continue;
    } else {
      res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
      if (res == ESP_OK) {
        size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
      }
      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      }
      esp_camera_fb_return(fb);
      fb = NULL;
    }

    if (res != ESP_OK) {
      break;
    }
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_uri_handlers = 4;
  config.lru_purge_enable = true;

  httpd_uri_t index_uri = {};
  index_uri.uri = "/";
  index_uri.method = HTTP_GET;
  index_uri.handler = index_handler;

  httpd_uri_t stream_uri = {};
  stream_uri.uri = "/stream";
  stream_uri.method = HTTP_GET;
  stream_uri.handler = stream_handler;

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  } else {
    Serial.println("Failed to start camera HTTP server");
  }
}

// APP_MODE_DORBEL: port 80 serves the page, /capture and the /audio
// intercom WebSocket; port 81 serves /stream on its own worker task.
void startDorbelServers(bool micOk, bool speakerOk) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_uri_handlers = 6;
  config.lru_purge_enable = true;
  config.close_fn = intercomOnClose;

  httpd_uri_t index_uri = {};
  index_uri.uri = "/";
  index_uri.method = HTTP_GET;
  index_uri.handler = dorbel_index_handler;

  httpd_uri_t capture_uri = {};
  capture_uri.uri = "/capture";
  capture_uri.method = HTTP_GET;
  capture_uri.handler = capture_handler;

  if (httpd_start(&control_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(control_httpd, &index_uri);
    httpd_register_uri_handler(control_httpd, &capture_uri);
    intercomBegin(control_httpd, micOk, speakerOk);
  } else {
    Serial.println("Failed to start control HTTP server");
  }

  httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
  stream_config.server_port = 81;
  stream_config.ctrl_port = 32769;
  stream_config.max_uri_handlers = 2;
  stream_config.lru_purge_enable = true;

  httpd_uri_t stream_uri = {};
  stream_uri.uri = "/stream";
  stream_uri.method = HTTP_GET;
  stream_uri.handler = stream_handler;

  if (httpd_start(&stream_httpd, &stream_config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  } else {
    Serial.println("Failed to start stream HTTP server");
  }
}
