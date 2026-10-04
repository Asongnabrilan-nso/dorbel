#pragma once

#include <Arduino.h>

// XIAO side of the UNO Q I2C link: slave at DORBEL_I2C_ADDR on D4/D5.
// The Wire callbacks queue each command; loop() drains them with
// i2cLinkNextCommand().
bool i2cLinkBegin();

// Status byte answered on every plain read. STATUS_TALK is OR-ed in while
// the UNO Q has sent CMD_TALK_ON.
void i2cLinkSetStatus(uint8_t flags);

// Address answered after CMD_GET_IP.
void i2cLinkSetIp(IPAddress ip);

// Next command from the UNO Q, false if none is waiting.
bool i2cLinkNextCommand(uint8_t *cmd);

uint32_t i2cLinkStatusReads();
bool i2cLinkTalkCommanded();
