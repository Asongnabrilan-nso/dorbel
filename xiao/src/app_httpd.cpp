#include <Arduino.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "camera_pins.h"

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

static httpd_handle_t stream_httpd = NULL;

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
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
