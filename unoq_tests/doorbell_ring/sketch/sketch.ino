// Dorbel step 12 - button -> UNO Q -> I2C -> XIAO -> MAX98357A -> speaker.
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

#define BUTTON_PIN 2
#define RED_PIN    3
#define GREEN_PIN  5
#define BLUE_PIN   6

#define LED_COMMON_ANODE 0

const unsigned long DEBOUNCE_MS     = 30;
const unsigned long RING_LOCKOUT_MS = 1500; // ignore re-presses while the chime plays
const unsigned long RING_LED_MS     = 1000;
const unsigned long STATUS_POLL_MS  = 1000;

int xiaoStatus = -1; // last status byte, -1 = no response
unsigned long lastRingAt = 0;
bool ringShown = false;

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

  if (buttonPressedEdge()) {
    if (ringShown && millis() - lastRingAt < RING_LOCKOUT_MS) {
      Monitor.println("Button pressed (ignored, chime still playing)");
    } else {
      bool ok = sendCommand(CMD_RING);
      lastRingAt = millis();
      ringShown = true;
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

  updateLed();
  delay(5);
}
