#include <Arduino.h>
#include <driver/i2s.h>
#include "mic_level.h"

// PDM microphone pins on the Sense expansion board
#define MIC_CLK  42
#define MIC_DATA 41

// Mic on I2S port 0, speaker on port 1 (see speaker_test.cpp).
#define MIC_I2S_PORT I2S_NUM_0

static const int SAMPLE_RATE = 16000;
static int16_t buffer[256];

// Port of the ESP_I2S reference sketch to the arduino-esp32 core 2.x legacy
// driver (ESP_I2S.h only exists in core 3.x): PDM RX, 16 kHz, 16-bit mono.
// Needs no SD card - it only reads samples and reports their level.
static bool initMic() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; // mono
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;

  if (i2s_driver_install(MIC_I2S_PORT, &config, 0, NULL) != ESP_OK) {
    return false;
  }

  // In PDM mode the WS line carries the PDM clock; there is no BCLK.
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_PIN_NO_CHANGE;
  pins.ws_io_num = MIC_CLK;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_DATA;
  return i2s_set_pin(MIC_I2S_PORT, &pins) == ESP_OK;
}

void setupMicLevel() {
  Serial.println("Mic level test: initializing PDM microphone (no SD card needed)...");
  if (!initMic()) {
    Serial.println("MIC INIT FAILED");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("MIC OK");
}

void loopMicLevel() {
  size_t bytesRead = 0;
  i2s_read(MIC_I2S_PORT, buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
  if (bytesRead == 0) {
    return;
  }

  int samples = bytesRead / sizeof(int16_t);

  // Remove the DC offset first so RMS tracks sound level, not the bias.
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += buffer[i];
  }
  float mean = (float)sum / samples;

  double energy = 0;
  for (int i = 0; i < samples; i++) {
    float v = buffer[i] - mean;
    energy += v * v;
  }
  float rms = sqrt(energy / samples);

  Serial.print("Mic RMS: ");
  Serial.println(rms);

  delay(50);
}
