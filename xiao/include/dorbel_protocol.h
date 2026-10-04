#pragma once

// Dorbel I2C control/status protocol between the UNO Q (master) and the
// XIAO (slave). Control and status only - audio and video go over Wi-Fi.
// Keep in sync with the constants in sketch/sketch.ino and unoq_tests/*/sketch/sketch.ino.

#define DORBEL_I2C_ADDR 0x08

// UNO Q -> XIAO: one command byte per write
#define CMD_PING     0x01
#define CMD_RING     0x02
#define CMD_TALK_ON  0x03
#define CMD_TALK_OFF 0x04

// XIAO -> UNO Q: one status byte per read, bit flags
#define STATUS_ALIVE  0x01 // XIAO firmware running (Wi-Fi state once integrated)
#define STATUS_CAMERA 0x02
#define STATUS_AUDIO  0x04
#define STATUS_TALK   0x08
