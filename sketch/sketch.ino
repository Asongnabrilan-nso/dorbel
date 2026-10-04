// Dorbel UNO Q MCU - doorbell button + RGB + I2C link to the XIAO + Bridge RPC API.
//
// Physical path:  button -> STM32 -> I2C -> XIAO -> speaker
// Software path:  STM32 -> Bridge RPC -> Python on Linux (python/main.py)
//
// RPC functions (provide_safe: they run between loop() iterations, so
// loop() must never block for long or Python's Bridge.call() stalls):
//   get_doorbell_state() -> 1 if a press happened since the last clear_event()
//   get_xiao_status()    -> last XIAO status byte, -1 if not responding
//   clear_event()        -> clears the doorbell latch, returns 1
//   get_xiao_ip()        -> XIAO IPv4 packed as a.b.c.d (a in the top byte), 0 if unknown
//
// Press the doorbell button: the UNO Q sends CMD_RING (0x02) to the XIAO,
// which plays the chime. The RGB LED shows system state:
//   GREEN = idle, XIAO answering
//   RED   = ring just sent
//   BLUE  = XIAO not answering on I2C (check wiring / XIAO firmware)
//
// Pins: D2 button (to GND), D3/D5/D6 RGB, A4/A5 I2C (Wire2) to XIAO D4/D5.

#include <Arduino_RouterBridge.h>
#include <Wire.h>

#define DORBEL_WIRE Wire2
#define XIAO_ADDR   0x08
#define CMD_RING    0x02
#define CMD_GET_IP  0x05

#define BUTTON_PIN 2
#define RED_PIN    3
#define GREEN_PIN  5
#define BLUE_PIN   6

#define LED_COMMON_ANODE 0

const unsigned long DEBOUNCE_MS     = 30;
const unsigned long RING_LOCKOUT_MS = 1500; // ignore re-presses while the chime plays
const unsigned long RING_LED_MS     = 1000;
const unsigned long STATUS_POLL_MS  = 1000;
const unsigned long IP_POLL_MS      = 5000;

int xiaoStatus = -1; // last status byte, -1 = no response
int xiaoIp = 0;      // packed IPv4 from the XIAO, 0 = unknown
unsigned long lastRingAt = 0;
bool ringShown = false;
bool doorbellEvent = false; // latched until Python calls clear_event()

int get_doorbell_state() {
  return doorbellEvent ? 1 : 0;
}

int get_xiao_status() {
  return xiaoStatus;
}

int clear_event() {
  doorbellEvent = false;
  return 1;
}

int get_xiao_ip() {
  return xiaoIp;
}

void setRGB(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r != LED_COMMON_ANODE);
  digitalWrite(GREEN_PIN, g != LED_COMMON_ANODE);
  digitalWrite(BLUE_PIN,  b != LED_COMMON_ANODE);
}

bool sendCommand(uint8_t cmd) {
  DORBEL_WIRE.beginTransmission(XIAO_ADDR);
  DORBEL_WIRE.write(cmd);
  return DORBEL_WIRE.endTransmission() == 0;
}

int readStatus() {
  if (DORBEL_WIRE.requestFrom(XIAO_ADDR, 1) == 1) {
    return DORBEL_WIRE.read();
  }
  return -1;
}

// The XIAO answers the read right after CMD_GET_IP with 4 address bytes.
int readIp() {
  if (!sendCommand(CMD_GET_IP)) {
    return 0;
  }
  if (DORBEL_WIRE.requestFrom(XIAO_ADDR, 4) != 4) {
    return 0;
  }
  uint32_t ip = 0;
  for (int i = 0; i < 4; i++) {
    ip = (ip << 8) | (uint8_t)DORBEL_WIRE.read();
  }
  return (int)ip;
}

// True once per press: falling edge that has stayed LOW for DEBOUNCE_MS.
bool buttonPressedEdge() {
  static bool stable = HIGH;
  static bool lastRaw = HIGH;
  static unsigned long changedAt = 0;

  bool raw = digitalRead(BUTTON_PIN);
  if (raw != lastRaw) {
    lastRaw = raw;
    changedAt = millis();
  }
  if (raw != stable && millis() - changedAt >= DEBOUNCE_MS) {
    stable = raw;
    return stable == LOW;
  }
  return false;
}

void updateLed() {
  if (ringShown && millis() - lastRingAt < RING_LED_MS) {
    setRGB(true, false, false);
  } else if (xiaoStatus < 0) {
    setRGB(false, false, true);
  } else {
    setRGB(false, true, false);
  }
}

void setup() {
  Bridge.begin();
  Bridge.provide_safe("get_doorbell_state", get_doorbell_state);
  Bridge.provide_safe("get_xiao_status", get_xiao_status);
  Bridge.provide_safe("clear_event", clear_event);
  Bridge.provide_safe("get_xiao_ip", get_xiao_ip);

  Monitor.begin();
  delay(1500); // Monitor drops output sent right after begin()

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  DORBEL_WIRE.begin();
  DORBEL_WIRE.setClock(100000);

  xiaoStatus = readStatus();
  Monitor.print("Doorbell ready, XIAO ");
  Monitor.println(xiaoStatus < 0 ? "NOT responding" : "OK");
  updateLed();
}

void loop() {
  static unsigned long lastPoll = 0;
  static unsigned long lastIpPoll = 0;

  if (buttonPressedEdge()) {
    if (ringShown && millis() - lastRingAt < RING_LOCKOUT_MS) {
      Monitor.println("Button pressed (ignored, chime still playing)");
    } else {
      bool ok = sendCommand(CMD_RING);
      lastRingAt = millis();
      ringShown = true;
      doorbellEvent = true;
      Monitor.print("DOORBELL PRESSED -> RING ");
      Monitor.println(ok ? "sent" : "FAILED (no ACK)");
    }
  }

  if (millis() - lastPoll >= STATUS_POLL_MS) {
    lastPoll = millis();
    int s = readStatus();
    if ((s < 0) != (xiaoStatus < 0)) {
      Monitor.println(s < 0 ? "XIAO stopped responding" : "XIAO back online");
    }
    xiaoStatus = s;
  }

  if (millis() - lastIpPoll >= IP_POLL_MS) {
    lastIpPoll = millis();
    xiaoIp = xiaoStatus < 0 ? 0 : readIp();
  }

  updateLed();
  delay(5);
}
