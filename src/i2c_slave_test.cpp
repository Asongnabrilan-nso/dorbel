#include <Arduino.h>
#include <Wire.h>
#include "dorbel_protocol.h"
#include "i2c_slave_test.h"
#include "speaker_test.h"

#define SDA_PIN 5 // XIAO D4
#define SCL_PIN 6 // XIAO D5

// Written from the Wire callbacks (I2C task context), read from loop().
static volatile uint8_t lastCommand = 0;
static volatile uint32_t commandCount = 0;
static volatile bool ringPending = false;
static volatile bool talkActive = false;
static volatile uint32_t requestCount = 0;
static bool speakerOk = false;

static void onReceive(int count) {
  if (count < 1) {
    return;
  }

  uint8_t cmd = Wire.read();
  switch (cmd) {
  case CMD_PING:
    break;
  case CMD_RING:
    ringPending = true;
    break;
  case CMD_TALK_ON:
    talkActive = true;
    break;
  case CMD_TALK_OFF:
    talkActive = false;
    break;
  default:
    cmd = 0xFF; // unknown - still logged below
    break;
  }
  lastCommand = cmd;
  commandCount++;

  while (Wire.available()) {
    Wire.read();
  }
}

static void onRequest() {
  // Camera is hard-coded "up" until the camera joins this mode; audio is
  // the real MAX98357A init result.
  uint8_t flags = STATUS_ALIVE | STATUS_CAMERA;
  if (speakerOk) {
    flags |= STATUS_AUDIO;
  }
  if (talkActive) {
    flags |= STATUS_TALK;
  }
  Wire.write(flags);
  requestCount++;
}

void setupI2cSlaveTest() {
  speakerOk = initSpeaker();
  Serial.println(speakerOk ? "MAX98357A OK" : "MAX98357A init FAILED - RING will be silent");

  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);

  if (!Wire.begin((uint8_t)DORBEL_I2C_ADDR, SDA_PIN, SCL_PIN, 100000)) {
    Serial.println("I2C SLAVE FAILED");
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("XIAO I2C slave ready at 0x%02X (SDA=GPIO%d/D4, SCL=GPIO%d/D5)\n",
                DORBEL_I2C_ADDR, SDA_PIN, SCL_PIN);
}

void loopI2cSlaveTest() {
  static uint32_t seenCommands = 0;
  static uint32_t lastReport = 0;

  if (commandCount != seenCommands) {
    seenCommands = commandCount;
    switch (lastCommand) {
    case CMD_PING:     Serial.println("PING received"); break;
    case CMD_RING:     Serial.println("RING COMMAND RECEIVED"); break;
    case CMD_TALK_ON:  Serial.println("TALK ON"); break;
    case CMD_TALK_OFF: Serial.println("TALK OFF"); break;
    default:           Serial.println("Unknown command received"); break;
    }
  }

  if (ringPending) {
    ringPending = false;
    if (speakerOk) {
      playChime(); // blocks ~0.9 s; I2C keeps answering from its own task
    }
  }

  if (millis() - lastReport > 5000) {
    lastReport = millis();
    Serial.printf("[stats] commands=%u status_reads=%u talk=%s\n", (unsigned)commandCount,
                  (unsigned)requestCount, talkActive ? "on" : "off");
  }

  delay(10);
}
