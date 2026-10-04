#include <Arduino.h>
#include <driver/i2s.h>
#include "speaker_test.h"

// MAX98357A wiring (same arrangement as the TechTalkies Xiaozhi-for-XiaoESP32S3
// project). D11/D12 are avoided because on the Sense board they are GPIO42/41,
// which the onboard PDM microphone already uses.
#define I2S_BCLK 7 // XIAO D8 -> MAX98357A BCLK
#define I2S_WS   4 // XIAO D3 -> MAX98357A LRC
#define I2S_DOUT 2 // XIAO D1 -> MAX98357A DIN

// The PDM mic uses I2S_NUM_0, so the speaker gets the second peripheral.
// That way both can run at the same time once the subsystems are integrated.
#define SPEAKER_I2S_PORT I2S_NUM_1

static const int SAMPLE_RATE = 16000;
static const int16_t AMPLITUDE = 5000; // ~15% of full scale - loud enough, no clipping

// This project is pinned to arduino-esp32 core 2.x (see platformio.ini), which
// does not ship the core 3.x ESP_I2S.h / I2SClass API, so the speaker is driven
// through the ESP-IDF legacy I2S driver instead. Behaviour is identical:
// standard (Philips) I2S, 16-bit, mono.
bool initSpeaker() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; // mono
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = true; // output silence (not a buzz) when starved

  if (i2s_driver_install(SPEAKER_I2S_PORT, &config, 0, NULL) != ESP_OK) {
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num = I2S_WS;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(SPEAKER_I2S_PORT, &pins) != ESP_OK) {
    return false;
  }

  i2s_zero_dma_buffer(SPEAKER_I2S_PORT);
  return true;
}

void playTone(float frequency, int durationMs) {
  const int samplesPerBuffer = 256;
  int16_t buffer[samplesPerBuffer];

  const int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
  float phase = 0.0f;
  const float step = 2.0f * PI * frequency / SAMPLE_RATE;

  int generated = 0;
  while (generated < totalSamples) {
    int count = min(samplesPerBuffer, totalSamples - generated);

    for (int i = 0; i < count; i++) {
      buffer[i] = (int16_t)(AMPLITUDE * sinf(phase));
      phase += step;
      if (phase >= 2.0f * PI) {
        phase -= 2.0f * PI;
      }
    }

    size_t written = 0;
    i2s_write(SPEAKER_I2S_PORT, buffer, count * sizeof(int16_t), &written, portMAX_DELAY);
    generated += count;
  }
}

void setupSpeakerTest() {
  Serial.println("Speaker test: initializing MAX98357A over I2S...");
  if (!initSpeaker()) {
    Serial.println("MAX98357A I2S init FAILED");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("MAX98357A OK");
}

void playChime() {
  playTone(880, 300); // A5 - "ding"
  delay(100);
  playTone(659, 500); // E5 - "dong"
}

void loopSpeakerTest() {
  Serial.println("Playing doorbell test...");
  playChime();
  delay(3000);
}
