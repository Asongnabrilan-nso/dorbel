// Dorbel step 10 - UNO Q (MCU) as I2C master to the XIAO slave at 0x08.
//
// Wiring (Dorbel uses A4/A5, NOT the D20/D21 SDA/SCL header):
//   UNO Q A4 (PC1, SDA) ── XIAO D4 / GPIO5
//   UNO Q A5 (PC0, SCL) ── XIAO D5 / GPIO6
//   UNO Q GND           ── XIAO GND
//   4.7k pull-ups from SDA and SCL to 3.3 V (one set for the whole bus)
//
// On the UNO Q Zephyr core: Wire = D20/D21 (i2c2), Wire1 = Qwiic (i2c4),
// Wire2 = A4/A5 (i2c3). Switch DORBEL_WIRE to Wire to use D20/D21 instead.
//
// Output goes to the App Lab console via Monitor (Arduino_RouterBridge);
// plain Serial only reaches the D0/D1 UART pins on this board.

#include <Arduino_RouterBridge.h>
#include <Wire.h>

#define DORBEL_WIRE Wire2
#define XIAO_ADDR   0x08

void sendCommand(uint8_t cmd) {
  DORBEL_WIRE.beginTransmission(XIAO_ADDR);
  DORBEL_WIRE.write(cmd);

  uint8_t result = DORBEL_WIRE.endTransmission();

  Monitor.print("Command ");
  Monitor.print(cmd, HEX);
  Monitor.print(" result = ");
  Monitor.println(result); // 0 = ACK, 2 = address NACK (wiring / pull-ups / XIAO not running)
}

void readStatus() {
  int count = DORBEL_WIRE.requestFrom(XIAO_ADDR, 1);

  if (count == 1) {
    uint8_t status = DORBEL_WIRE.read();

    Monitor.print("XIAO status: 0x");
    if (status < 0x10) Monitor.print("0");
    Monitor.println(status, HEX);
  } else {
    Monitor.println("No XIAO response");
  }
}

void setup() {
  Monitor.begin();
  delay(1500); // Monitor drops output sent right after begin()

  DORBEL_WIRE.begin();
  DORBEL_WIRE.setClock(100000);

  Monitor.println("UNO Q I2C master ready");

  sendCommand(0x01); // PING
}

void loop() {
  readStatus();

  delay(1000);

  sendCommand(0x02); // test ring command

  delay(3000);
}
