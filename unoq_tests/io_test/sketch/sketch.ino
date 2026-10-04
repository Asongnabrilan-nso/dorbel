// Dorbel step 11 - UNO Q local I/O test: doorbell button + RGB status LED.
//
//   D2 ── button ── GND     (INPUT_PULLUP, pressed = LOW)
//   D3 ──220Ω── RED
//   D5 ──220Ω── GREEN
//   D6 ──220Ω── BLUE
//   GND ─────── common cathode   (common-anode: set LED_COMMON_ANODE to 1)
//
// BOOT -> GREEN, button held -> RED. (No reed switch / door sensor in V1.)

#include <Arduino_RouterBridge.h>

#define BUTTON_PIN 2
#define RED_PIN    3
#define GREEN_PIN  5
#define BLUE_PIN   6

#define LED_COMMON_ANODE 0

void setRGB(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r != LED_COMMON_ANODE);
  digitalWrite(GREEN_PIN, g != LED_COMMON_ANODE);
  digitalWrite(BLUE_PIN,  b != LED_COMMON_ANODE);
}

void setup() {
  Monitor.begin();
  delay(1500); // Monitor drops output sent right after begin()

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  setRGB(false, true, false);
  Monitor.println("IO test ready - GREEN = idle");
}

void loop() {
  bool buttonPressed = digitalRead(BUTTON_PIN) == LOW;

  if (buttonPressed) {
    setRGB(true, false, false);
    Monitor.println("DOORBELL PRESSED");
    delay(300);
  } else {
    setRGB(false, true, false);
  }

  delay(50);
}
