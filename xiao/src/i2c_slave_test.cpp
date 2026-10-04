#include <Arduino.h>
#include "dorbel_protocol.h"
#include "i2c_link.h"
#include "i2c_slave_test.h"
#include "speaker_test.h"

static bool speakerOk = false;

void setupI2cSlaveTest() {
  speakerOk = initSpeaker();
  Serial.println(speakerOk ? "MAX98357A OK" : "MAX98357A init FAILED - RING will be silent");

  // Camera is hard-coded "up" until the camera joins this mode; audio is
  // the real MAX98357A init result.
  i2cLinkSetStatus(STATUS_ALIVE | STATUS_CAMERA | (speakerOk ? STATUS_AUDIO : 0));

  if (!i2cLinkBegin()) {
    Serial.println("I2C SLAVE FAILED");
    while (true) {
      delay(1000);
    }
  }
}

void loopI2cSlaveTest() {
  static uint32_t commandCount = 0;
  static uint32_t lastReport = 0;

  bool ring = false;
  uint8_t cmd;
  while (i2cLinkNextCommand(&cmd)) {
    commandCount++;
    switch (cmd) {
    case CMD_PING:     Serial.println("PING received"); break;
    case CMD_RING:     Serial.println("RING COMMAND RECEIVED"); ring = true; break;
    case CMD_TALK_ON:  Serial.println("TALK ON"); break;
    case CMD_TALK_OFF: Serial.println("TALK OFF"); break;
    case CMD_GET_IP:   Serial.println("GET_IP received"); break;
    default:           Serial.println("Unknown command received"); break;
    }
  }

  if (ring && speakerOk) {
    playChime(); // blocks ~0.9 s; I2C keeps answering from its own task
  }

  if (millis() - lastReport > 5000) {
    lastReport = millis();
    Serial.printf("[stats] commands=%u status_reads=%u talk=%s\n", (unsigned)commandCount,
                  (unsigned)i2cLinkStatusReads(), i2cLinkTalkCommanded() ? "on" : "off");
  }

  delay(10);
}
