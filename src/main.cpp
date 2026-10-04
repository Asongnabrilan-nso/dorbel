#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "camera_pins.h"
#include "secrets.h"
#include "mic_capture.h"
#include "speaker_test.h"
#include "mic_level.h"

// ---------------------------------------------------------------------
// Pick which subsystem test to run. Change this line and reflash to
// switch - each mode brings up one Dorbel subsystem on its own so it can
// be verified before final integration (camera and mic/SD share some
// GPIOs on the Sense board, see mic_capture.cpp).
// ---------------------------------------------------------------------
#define APP_MODE_CAMERA_STREAM 1
#define APP_MODE_MIC_CAPTURE   2
#define APP_MODE_SPEAKER_TEST  3
#define APP_MODE_MIC_LEVEL     4
#define APP_MODE APP_MODE_MIC_LEVEL

void startCameraServer();
void setupLedFlash(int pin);

static void configureCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound()) {
    config.jpeg_quality = 10; // lower = better quality, larger frames
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Fallback if PSRAM somehow isn't detected - keeps things working,
    // just at lower resolution using internal RAM.
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    while (true) {
      delay(1000);
    }
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);     // flip it back
    sensor->set_brightness(sensor, 1); // up the brightness just a bit
    sensor->set_saturation(sensor, -2); // lower the saturation
  }
  // The sensor on the Sense board sits rotated relative to the enclosure -
  // flip vertically so the stream comes out right-side up.
  sensor->set_vflip(sensor, 1);
  // Drop down frame size for a higher initial frame rate; the web UI /
  // client can still request the full UXGA frame buffer size later.
  sensor->set_framesize(sensor, FRAMESIZE_QVGA);

#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif
}

static void setupCameraStreamMode() {
  Serial.println("Mode: camera live stream");

  configureCamera();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Scanning for WiFi networks...");
  int found = WiFi.scanNetworks();
  if (found <= 0) {
    Serial.println("  No networks found at all - check the board is powered "
                    "well and the hotspot is broadcasting on 2.4GHz.");
  } else {
    bool targetSeen = false;
    for (int i = 0; i < found; i++) {
      Serial.printf("  [%d] %s (RSSI %d, ch %d, %s)\n", i, WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i), WiFi.channel(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
      if (WiFi.SSID(i) == WIFI_SSID) {
        targetSeen = true;
      }
    }
    if (!targetSeen) {
      Serial.printf("  Target SSID \"%s\" was NOT in the scan results - the "
                    "ESP32 can't see it. Likely a 5GHz-only hotspot, out of "
                    "range, or the SSID string doesn't match exactly.\n",
                    WIFI_SSID);
    }
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false); // avoid WiFi power-save stutter while streaming
  Serial.print("Connecting to WiFi");
  uint32_t attemptStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - attemptStart > 15000) {
      Serial.printf("\nStill not connected after 15s, WiFi.status() = %d\n",
                    WiFi.status());
      attemptStart = millis();
    }
  }
  Serial.println();

  Serial.print("Connected! Open http://");
  Serial.print(WiFi.localIP());
  Serial.println("/ in a browser on the same network to view the stream.");

  startCameraServer();
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // wait for the native USB CDC host to attach
  }
  Serial.println();

#if APP_MODE == APP_MODE_CAMERA_STREAM
  setupCameraStreamMode();
#elif APP_MODE == APP_MODE_MIC_CAPTURE
  setupMicCapture();
#elif APP_MODE == APP_MODE_SPEAKER_TEST
  setupSpeakerTest();
#elif APP_MODE == APP_MODE_MIC_LEVEL
  setupMicLevel();
#else
#error "APP_MODE must be one of the APP_MODE_* values above"
#endif
}

void loop() {
#if APP_MODE == APP_MODE_SPEAKER_TEST
  loopSpeakerTest();
#elif APP_MODE == APP_MODE_MIC_LEVEL
  loopMicLevel();
#else
  // Everything happens in the HTTP server's own task (camera mode), or
  // setup() already finished its one-shot recording (mic mode).
  delay(10000);
#endif
}
