#include <Arduino.h>
#include <Wire.h>
#include "dorbel_protocol.h"
#include "i2c_link.h"

#define SDA_PIN 5 // XIAO D4
#define SCL_PIN 6 // XIAO D5

// The Wire callbacks run in the I2C driver's task (not an ISR), so a FreeRTOS
// queue is safe to use from them.
static QueueHandle_t commandQueue = NULL;
static volatile uint8_t statusFlags = 0;
static volatile uint32_t ipAddress = 0;
static volatile bool ipRequested = false;
static volatile bool talkCommanded = false;
static volatile uint32_t statusReads = 0;

static void onReceive(int count) {
  if (count < 1) {
    return;
  }

  uint8_t cmd = Wire.read();
  while (Wire.available()) {
    Wire.read();
  }

  // Only the read that directly follows CMD_GET_IP returns the address.
  ipRequested = cmd == CMD_GET_IP;

  switch (cmd) {
  case CMD_PING:
  case CMD_RING:
  case CMD_GET_IP:
    break;
  case CMD_TALK_ON:
    talkCommanded = true;
    break;
  case CMD_TALK_OFF:
    talkCommanded = false;
    break;
  default:
    cmd = 0xFF; // unknown - still reported to loop()
    break;
  }
  xQueueSend(commandQueue, &cmd, 0); // dropped if loop() falls far behind
}

static void onRequest() {
  if (ipRequested) {
    ipRequested = false;
    IPAddress ip(ipAddress);
    uint8_t bytes[4] = {ip[0], ip[1], ip[2], ip[3]};
    Wire.write(bytes, sizeof(bytes));
    return;
  }

  uint8_t flags = statusFlags;
  if (talkCommanded) {
    flags |= STATUS_TALK;
  }
  Wire.write(flags);
  statusReads++;
}

bool i2cLinkBegin() {
  commandQueue = xQueueCreate(16, sizeof(uint8_t));
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);

  if (!Wire.begin((uint8_t)DORBEL_I2C_ADDR, SDA_PIN, SCL_PIN, 100000)) {
    return false;
  }
  Serial.printf("XIAO I2C slave ready at 0x%02X (SDA=GPIO%d/D4, SCL=GPIO%d/D5)\n",
                DORBEL_I2C_ADDR, SDA_PIN, SCL_PIN);
  return true;
}

void i2cLinkSetStatus(uint8_t flags) {
  statusFlags = flags;
}

void i2cLinkSetIp(IPAddress ip) {
  ipAddress = (uint32_t)ip;
}

bool i2cLinkNextCommand(uint8_t *cmd) {
  return commandQueue && xQueueReceive(commandQueue, cmd, 0) == pdTRUE;
}

uint32_t i2cLinkStatusReads() {
  return statusReads;
}

bool i2cLinkTalkCommanded() {
  return talkCommanded;
}
