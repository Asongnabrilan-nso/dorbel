#include <Arduino.h>
#include <lwip/sockets.h>
#include <freertos/stream_buffer.h>
#include "intercom.h"
#include "mic_level.h"
#include "speaker_test.h"

static const size_t MIC_FRAME_SAMPLES = 512;      // 32 ms at 16 kHz
static const int MIC_GAIN = 8;                    // raw PDM output is quiet
static const size_t SPEAKER_BUFFER_BYTES = 16000; // 0.5 s of incoming talk audio
static const size_t PREBUFFER_BYTES = 3200;       // 100 ms cushion against Wi-Fi jitter
static const uint32_t TALK_TIMEOUT_MS = 1000;     // talk ends if the browser goes quiet

static httpd_handle_t server = NULL;
static StreamBufferHandle_t speakerBuffer = NULL;
static volatile int clientFd = -1;
static volatile bool talking = false;
static volatile uint32_t lastTalkAudioAt = 0;
static volatile bool chimeRequested = false;
static volatile bool chimePlaying = false;
static uint8_t rxBuffer[4096]; // only touched from the httpd task

static esp_err_t audio_ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    // Handshake done: the newest browser takes over the intercom.
    clientFd = httpd_req_to_sockfd(req);
    talking = false;
    Serial.printf("Intercom client connected (fd %d)\n", clientFd);
    return ESP_OK;
  }

  httpd_ws_frame_t frame = {};
  esp_err_t err = httpd_ws_recv_frame(req, &frame, 0); // length only
  if (err != ESP_OK) {
    return err;
  }
  if (frame.len > sizeof(rxBuffer)) {
    Serial.printf("Intercom frame too large (%u bytes), closing\n", (unsigned)frame.len);
    return ESP_FAIL;
  }
  if (frame.len == 0) {
    return ESP_OK;
  }
  frame.payload = rxBuffer;
  err = httpd_ws_recv_frame(req, &frame, frame.len);
  if (err != ESP_OK) {
    return err;
  }

  if (httpd_req_to_sockfd(req) != clientFd) {
    return ESP_OK; // a newer browser has taken over
  }

  if (frame.type == HTTPD_WS_TYPE_TEXT) {
    if (frame.len >= 6 && memcmp(rxBuffer, "talk:1", 6) == 0) {
      talking = true;
      lastTalkAudioAt = millis();
    } else if (frame.len >= 6 && memcmp(rxBuffer, "talk:0", 6) == 0) {
      talking = false;
    }
  } else if (frame.type == HTTPD_WS_TYPE_BINARY && talking && !chimePlaying) {
    lastTalkAudioAt = millis();
    xStreamBufferSend(speakerBuffer, rxBuffer, frame.len & ~1u, 0); // drop if full
  }
  return ESP_OK;
}

void intercomOnClose(httpd_handle_t hd, int sockfd) {
  if (sockfd == clientFd) {
    clientFd = -1;
    talking = false;
    Serial.println("Intercom client disconnected");
  }
  close(sockfd); // with a custom close_fn the server leaves this to us
}

static void micTask(void *) {
  static int16_t samples[MIC_FRAME_SAMPLES];
  float dc = 0;

  for (;;) {
    size_t n = micRead(samples, MIC_FRAME_SAMPLES);

    // One-pole DC blocker, then gain with clipping.
    for (size_t i = 0; i < n; i++) {
      float s = samples[i];
      dc += (s - dc) * 0.001f;
      int32_t v = (int32_t)((s - dc) * MIC_GAIN);
      samples[i] = (int16_t)constrain(v, -32768, 32767);
    }

    int fd = clientFd;
    if (n == 0 || fd < 0 || talking || chimePlaying) {
      continue;
    }
    if (httpd_ws_get_fd_info(server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
      continue;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_BINARY;
    frame.final = true;
    frame.payload = (uint8_t *)samples;
    frame.len = n * sizeof(int16_t);
    httpd_ws_send_frame_async(server, fd, &frame);
  }
}

static void speakerTask(void *) {
  static int16_t samples[256];
  bool playing = false;

  for (;;) {
    if (chimeRequested) {
      chimeRequested = false;
      chimePlaying = true;
      xStreamBufferReset(speakerBuffer);
      playChime();
      chimePlaying = false;
      playing = false;
      continue;
    }

    if (talking && millis() - lastTalkAudioAt > TALK_TIMEOUT_MS) {
      talking = false;
    }

    if (!playing) {
      size_t queued = xStreamBufferBytesAvailable(speakerBuffer);
      // Wait for a cushion before starting, but flush the tail once talk ends.
      if (queued < PREBUFFER_BYTES && (talking || queued == 0)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      playing = true;
    }

    size_t got = xStreamBufferReceive(speakerBuffer, samples, sizeof(samples), pdMS_TO_TICKS(50));
    if (got < sizeof(int16_t)) {
      playing = false; // underrun: build the cushion up again
      continue;
    }
    speakerWrite(samples, got / sizeof(int16_t));
  }
}

void intercomBegin(httpd_handle_t httpServer, bool micOk, bool speakerOk) {
  server = httpServer;
  speakerBuffer = xStreamBufferCreate(SPEAKER_BUFFER_BYTES, 1);

  httpd_uri_t audio_uri = {};
  audio_uri.uri = "/audio";
  audio_uri.method = HTTP_GET;
  audio_uri.handler = audio_ws_handler;
  audio_uri.is_websocket = true;
  httpd_register_uri_handler(server, &audio_uri);

  // Core 1, away from Wi-Fi and the camera on core 0.
  if (micOk) {
    xTaskCreatePinnedToCore(micTask, "mic", 4096, NULL, 5, NULL, 1);
  }
  if (speakerOk) {
    xTaskCreatePinnedToCore(speakerTask, "speaker", 4096, NULL, 5, NULL, 1);
  }
}

void intercomRequestChime() {
  chimeRequested = true;
}

bool intercomTalking() {
  return talking;
}

bool intercomClientConnected() {
  return clientFd >= 0;
}
