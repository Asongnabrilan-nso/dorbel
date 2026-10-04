#include <Arduino.h>
#include <I2S.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "mic_capture.h"

// PDM microphone pins on the Sense expansion board
#define MIC_CLK_PIN 42
#define MIC_DATA_PIN 41

// microSD card CS pin on the Sense expansion board. This shares GPIO21 with
// the camera's flash LED (a documented XIAO ESP32S3 Sense hardware quirk) -
// harmless here since camera mode and mic mode never run at the same time.
#define SD_CS_PIN 21

#define RECORD_SECONDS  10U
#define WAV_FILE_PATH   "/mic_test.wav"
#define SAMPLE_RATE     16000U
#define SAMPLE_BITS     16
#define WAV_HEADER_SIZE 44
#define VOLUME_GAIN     2 // raw PDM output is quiet; left-shift to boost it

static void generateWavHeader(uint8_t *header, uint32_t dataSize, uint32_t sampleRate) {
  uint32_t fileSize = dataSize + WAV_HEADER_SIZE - 8;
  uint32_t byteRate = sampleRate * SAMPLE_BITS / 8;
  const uint8_t fields[] = {
      'R', 'I', 'F', 'F',
      (uint8_t)fileSize, (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
      'W', 'A', 'V', 'E',
      'f', 'm', 't', ' ',
      0x10, 0x00, 0x00, 0x00, // fmt chunk size = 16
      0x01, 0x00,             // PCM format
      0x01, 0x00,             // mono
      (uint8_t)sampleRate, (uint8_t)(sampleRate >> 8), (uint8_t)(sampleRate >> 16), (uint8_t)(sampleRate >> 24),
      (uint8_t)byteRate, (uint8_t)(byteRate >> 8), (uint8_t)(byteRate >> 16), (uint8_t)(byteRate >> 24),
      0x02, 0x00, // block align
      0x10, 0x00, // bits per sample
      'd', 'a', 't', 'a',
      (uint8_t)dataSize, (uint8_t)(dataSize >> 8), (uint8_t)(dataSize >> 16), (uint8_t)(dataSize >> 24),
  };
  memcpy(header, fields, sizeof(fields));
}

void setupMicCapture() {
  Serial.println("Mic capture test: initializing PDM microphone...");
  I2S.setAllPins(-1, MIC_CLK_PIN, MIC_DATA_PIN, -1, -1);
  if (!I2S.begin(PDM_MONO_MODE, SAMPLE_RATE, SAMPLE_BITS)) {
    Serial.println("Failed to initialize I2S/PDM microphone!");
    while (true) {
      delay(1000);
    }
  }

  // The SD card is optional: without one the clip is still recorded to PSRAM
  // and summarized over serial, so the mic can be verified card-free.
  Serial.println("Mounting SD card...");
  bool sdOk = SD.begin(SD_CS_PIN);
  if (!sdOk) {
    Serial.println("No SD card (or not FAT32) - will record and print levels only.");
  }

  uint32_t recordBytes = SAMPLE_RATE * (SAMPLE_BITS / 8) * RECORD_SECONDS;
  uint8_t *recordBuffer = (uint8_t *)ps_malloc(recordBytes);
  if (!recordBuffer) {
    Serial.println("Failed to allocate PSRAM recording buffer!");
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("Recording %u seconds (%u bytes) - speak now...\n", RECORD_SECONDS, recordBytes);
  size_t bytesRead = 0;
  esp_i2s::i2s_read(esp_i2s::I2S_NUM_0, recordBuffer, recordBytes, &bytesRead, portMAX_DELAY);
  Serial.printf("Captured %u bytes\n", (unsigned)bytesRead);

  for (uint32_t i = 0; i + 1 < bytesRead; i += 2) {
    *(uint16_t *)(recordBuffer + i) <<= VOLUME_GAIN;
  }

  if (!sdOk) {
    // Per-second RMS (DC removed) so silence vs. speech is visible.
    const int16_t *samples = (const int16_t *)recordBuffer;
    size_t total = bytesRead / sizeof(int16_t);
    for (size_t start = 0; start < total; start += SAMPLE_RATE) {
      size_t n = min((size_t)SAMPLE_RATE, total - start);
      double mean = 0, energy = 0;
      for (size_t i = 0; i < n; i++) {
        mean += samples[start + i];
      }
      mean /= n;
      for (size_t i = 0; i < n; i++) {
        double v = samples[start + i] - mean;
        energy += v * v;
      }
      Serial.printf("  second %u: RMS %.1f\n", (unsigned)(start / SAMPLE_RATE) + 1, sqrt(energy / n));
    }
    free(recordBuffer);
    Serial.println("Recording not saved (no SD card). Use APP_MODE_MIC_LEVEL for a live meter.");
    return;
  }

  SD.remove(WAV_FILE_PATH);
  File file = SD.open(WAV_FILE_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open WAV file for writing!");
    free(recordBuffer);
    return;
  }

  uint8_t header[WAV_HEADER_SIZE];
  generateWavHeader(header, bytesRead, SAMPLE_RATE);
  file.write(header, WAV_HEADER_SIZE);
  file.write(recordBuffer, bytesRead);
  file.close();
  free(recordBuffer);

  Serial.printf("Saved %s (%u bytes audio + %u byte header)\n", WAV_FILE_PATH,
                (unsigned)bytesRead, WAV_HEADER_SIZE);
  Serial.println("Pull the SD card and play the WAV file on a computer to verify it.");
}
