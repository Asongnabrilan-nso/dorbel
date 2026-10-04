# Dorbel

Dorbel is a private smart doorbell that does its processing at the edge. It is built on an
**Arduino UNO Q (4 GB)** as the system brain and a **Seeed XIAO ESP32S3 Sense** as the
multimedia endpoint (camera, microphone, speaker).

The application layer (Flask dashboard, video streaming, Telegram notifications, audio) follows
[Q04 Trillo](https://gitlab.com/supermoderno/q04_trillo). Dorbel does **not** use Trillo's Media
Carrier / IMX219 hardware path. Camera and audio come from the XIAO over Wi-Fi.
The speaker wiring follows
[Xiaozhi-for-XiaoESP32S3](https://github.com/TechTalkies/Xiaozhi-for-XiaoESP32S3).

---

## Architecture (V1, locked)

```
                    ┌──────────────────────┐
                    │        DORBEL        │
                    └──────────┬───────────┘
                ┌──────────────┴──────────────┐
                ▼                             ▼
       XIAO ESP32S3 Sense              Arduino UNO Q 4GB
       Multimedia endpoint                System brain
       ┌────────┼─────────┐               ┌────┴─────┐
       ▼        ▼         ▼               ▼          ▼
    CAMERA     MIC      I2S OUT         BUTTON      RGB
       └────────┴─────────┘
                │ Wi-Fi
                ▼
         UNO Q / Browser
        ┌───────┴─────────┐
        ▼                 ▼
     Dashboard         Telegram

XIAO ←──────── I²C control/status ────────→ UNO Q
```

**Main rule:** I²C carries **control and status only**. Wi-Fi carries **video and two-way
audio**. Camera frames and audio never go over I²C.

---

## Pin map

### XIAO ESP32S3 Sense

| Function                | XIAO pin | GPIO   | Connects to         |
|-------------------------|----------|--------|---------------------|
| I²C SDA (slave, `0x08`) | D4       | GPIO5  | UNO Q A4 (SDA)      |
| I²C SCL                 | D5       | GPIO6  | UNO Q A5 (SCL)      |
| PDM mic clock (onboard) | —        | GPIO42 | onboard             |
| PDM mic data (onboard)  | —        | GPIO41 | onboard             |
| I2S BCLK                | D8       | GPIO7  | MAX98357A BCLK      |
| I2S LRC / WS            | D3       | GPIO4  | MAX98357A LRC       |
| I2S DOUT                | D1       | GPIO2  | MAX98357A DIN       |
| 5V                      | 5V       | —      | MAX98357A VIN       |
| GND                     | GND      | —      | MAX98357A GND, UNO Q GND |

D11/D12 are avoided for audio because on the Sense board they are GPIO42/GPIO41, which the
onboard microphone uses. Camera pins are listed in `include/camera_pins.h`.

### MAX98357A → speaker

```
MAX98357A SPK+ ──┐
                 ├── 8 Ω / 2 W speaker
MAX98357A SPK- ──┘
```

> ⚠️ The MAX98357A output is bridge-tied. Connect the speaker between **SPK+ and SPK-**. Never
> connect SPK- to GND.

### Arduino UNO Q

| Function      | UNO Q pin | Notes                                            |
|---------------|-----------|--------------------------------------------------|
| Push button   | D2        | to GND, `INPUT_PULLUP`                           |
| RGB red       | D3        | via 220 Ω                                        |
| RGB green     | D5        | via 220 Ω                                        |
| RGB blue      | D6        | via 220 Ω                                        |
| I²C SDA       | **A4** (D18, PC1) | to XIAO D4. `Wire2` in the sketch, **not** the D20/D21 header (`Wire`) |
| I²C SCL       | **A5** (D19, PC0) | to XIAO D5                                       |

The RGB LED is assumed to be common-cathode, with the common pin to GND. If yours is
common-anode, set `LED_COMMON_ANODE 1` in the UNO Q sketches.

V1 has no door sensor. The reed switch on D7 from the original plan has been dropped.

---

## I²C protocol

- **UNO Q = master** (`Wire2.begin()`, A4/A5). The UNO Q docs demonstrate the board as master,
  so it is the lower-risk choice.
- **XIAO = slave at `0x08`**, using `Wire.onReceive()` / `Wire.onRequest()`.

| Direction    | Message        | Value  |
|--------------|----------------|--------|
| UNO Q → XIAO | `CMD_PING`     | `0x01` |
| UNO Q → XIAO | `CMD_RING`     | `0x02` |
| UNO Q → XIAO | `CMD_TALK_ON`  | `0x03` |
| UNO Q → XIAO | `CMD_TALK_OFF` | `0x04` |
| XIAO → UNO Q | status byte (bit flags) | `STATUS_ALIVE` 0x01 (becomes Wi-Fi up later), `STATUS_CAMERA` 0x02, `STATUS_AUDIO` 0x04, `STATUS_TALK` 0x08 |

---

## Power

```
            8–12 V BATTERY
                  │
          ┌───────┴────────┐
          ▼                ▼
      UNO Q VIN         5 V BUCK
      (7–24 V)             │
                    ┌──────┴──────┐
                    ▼             ▼
                  XIAO        MAX98357A
                    └──── GND ────┘   (all grounds common)
```

Do not feed the battery directly into the XIAO or the MAX98357A.

**Bench setup during development:** power the UNO Q and the XIAO from USB-C each, and power the
MAX98357A from the XIAO's 5V pin.

---

## Code base

The repository holds two firmware targets:

- **XIAO ESP32S3 Sense:** a PlatformIO project at the repo root.
- **UNO Q:** Arduino App Lab apps under `unoq/`. Each app has an STM32 sketch and a Linux-side
  Python script.

```
platformio.ini            XIAO: board, PSRAM (qio_opi), huge_app partition, USB-CDC serial
include/
  camera_pins.h           OV2640 pin map for the Sense board
  dorbel_protocol.h       I²C address, CMD_* and STATUS_* values (mirrored in unoq sketches)
  mic_capture.h           mic WAV capture API
  mic_level.h             live mic level meter API
  speaker_test.h          speaker API: initSpeaker(), playTone(), playChime()
  i2c_slave_test.h        I²C slave API
  secrets.example.h       Wi-Fi credentials template → copy to secrets.h (gitignored)
src/
  main.cpp                APP_MODE selector + camera setup + Wi-Fi connect
  app_httpd.cpp           MJPEG HTTP server: "/" page and "/stream"
  speaker_test.cpp        MAX98357A over I2S port 1: tones + "ding-dong" chime
  mic_level.cpp           live mic RMS meter over serial (no SD card needed)
  mic_capture.cpp         10 s mic clip: /mic_test.wav if an SD card is present, else RMS over serial
  i2c_slave_test.cpp      I²C slave at 0x08: logs commands, answers status, plays chime on RING
unoq/                     UNO Q App Lab apps (app.yaml + sketch/ + python/)
  i2c_master_test/        step 10: PING + RING over I²C, prints XIAO status
  io_test/                step 11: button + RGB LED
  doorbell_ring/          step 12: button → I²C RING → XIAO chime
  dorbel_bridge/          steps 13–14: step 12 + Bridge RPC API + python/main.py poller
stl/                      enclosure models (base, cover, body)
```

### Selecting a subsystem test

Each subsystem is tested on its own before integration. To pick one, edit this line in
`src/main.cpp`:

```cpp
#define APP_MODE APP_MODE_MIC_LEVEL   // or APP_MODE_CAMERA_STREAM / APP_MODE_MIC_CAPTURE / APP_MODE_SPEAKER_TEST
#define APP_MODE APP_MODE_I2C_SLAVE   // or _SPEAKER_TEST / _MIC_LEVEL / _MIC_CAPTURE / _CAMERA_STREAM

### Arduino core version

The project is pinned to **arduino-esp32 core 2.x** (2.0.17, the default for PlatformIO's
`espressif32` platform). On this core:

- I2S uses the legacy `driver/i2s.h` and `I2S.h`. The core 3.x `ESP_I2S.h` / `I2SClass` API
  **does not exist** here.
- LEDC uses `ledcSetup()` + `ledcAttachPin()`.

The speaker test is a direct port of the reference `ESP_I2S` sketch: same pins, 16 kHz,
16-bit mono, standard I2S, same tones. If you later move to core 3.x, port the I2S and LEDC
calls together.

The microphone uses I2S port 0 and the speaker uses **I2S port 1**, so both can run at the same
time after integration.

### Build and flash

```bash
cp include/secrets.example.h include/secrets.h   # first time only; fill in Wi-Fi
pio run                      # build
pio run -t upload            # flash (close any open serial monitor first)
pio device monitor           # 115200 baud
```

PlatformIO's CLI is at `~/.platformio/penv/Scripts/pio.exe` if `pio` is not on your PATH.

> Serial runs over native USB. `setup()` waits for a USB host (`while (!Serial)`), so for now
> the firmware only starts when the board is connected to a computer. Remove that wait before
> running on battery.

---

## Build order and test status

| Step | Subsystem                                   | Board | Code                                          | Status |
|------|---------------------------------------------|-------|-----------------------------------------------|--------|
| 1    | MAX98357A speaker                           | XIAO  | `APP_MODE_SPEAKER_TEST`                        | Builds, needs a test on the board |
| 2    | Onboard PDM mic, live RMS (no SD)           | XIAO  | `APP_MODE_MIC_LEVEL`                           | Running on the board; voice response to confirm |
| 2b   | PDM mic → WAV clip (SD optional)            | XIAO  | `APP_MODE_MIC_CAPTURE`                         | Builds; SD-free fallback untested |
| 3    | Camera MJPEG stream → browser               | XIAO  | `APP_MODE_CAMERA_STREAM`                       | Builds, re-test on the board |
| 4/10 | I²C link: UNO Q master ↔ XIAO `0x08`        | both  | `APP_MODE_I2C_SLAVE` + `unoq/i2c_master_test`  | XIAO flashed and running; UNO Q compiles, needs a test on the board |
| 11   | Button + RGB LED                            | UNO Q | `unoq/io_test`                                 | Compiles, needs a test on the board |
| 12   | Button → I²C RING → XIAO chime              | both  | `unoq/doorbell_ring` + `APP_MODE_I2C_SLAVE`    | Compiles, needs a test on the board |
| 13–14| STM32 ↔ Linux Bridge RPC + Python poller    | UNO Q | `unoq/dorbel_bridge`                           | Compiles, needs a test on the board |
| –    | Wi-Fi audio (mic up, speaker down)          | XIAO  | —                                              | Planned |
| –    | Flask dashboard + Telegram (from Trillo)    | UNO Q | —                                              | Planned |
| –    | Integration + battery power                 | both  | —                                              | Planned |

### Subsystem 1: MAX98357A speaker test

1. Wire BCLK→D8, LRC→D3, DIN→D1, VIN→5V and GND→GND. Connect the speaker across SPK+/SPK-.
2. Set `APP_MODE_SPEAKER_TEST`, then flash.
3. Open the serial monitor. You should see `MAX98357A OK`, followed by `Playing doorbell test...`
   every ~3.9 s.
4. Pass condition: you hear a clean two-tone chime (880 Hz for 300 ms, then 659 Hz for 500 ms).

| Symptom                     | Likely cause                                                       |
|-----------------------------|--------------------------------------------------------------------|
| `I2S init FAILED`           | I2S port or pin conflict. Check the pin defines in `speaker_test.cpp`. |
| Logs are fine but no sound  | VIN/GND not connected, BCLK and LRC swapped, or SD pin pulled low (shutdown mode). |
| Loud buzz or distortion     | Speaker wired SPK-→GND, or amplitude too high. Lower `AMPLITUDE`.   |
| Too quiet                   | Raise `AMPLITUDE` (max 32767), or run VIN at 5 V instead of 3.3 V.  |
| No serial output            | Firmware waits for USB CDC. Reopen the monitor or press reset.     |

### Subsystem 2: onboard microphone (no SD card needed)

The mic is the Sense board's PDM mic: clock on GPIO42, data on GPIO41, read on I2S port 0 at
16 kHz, 16-bit mono. The reference sketch uses the core 3.x `ESP_I2S` API, so it was ported to
the core 2.x `driver/i2s.h` API. In PDM mode the clock goes on the WS pin.

1. Set `APP_MODE_MIC_LEVEL`, then flash. No microSD card is required.
2. Open the serial monitor. You should see `MIC OK`, followed by a stream of `Mic RMS: <value>`
   lines (about 15 per second).
3. Pass condition: the value stays low when the room is quiet and jumps when you speak or clap
   near the board. The DC offset is subtracted, so RMS reflects sound only.

Measured on the bench: quiet room ≈ 5–25.

`APP_MODE_MIC_CAPTURE` no longer stops when no card is inserted. It still records 10 s into
PSRAM. If a card is mounted, it saves `/mic_test.wav`. Otherwise it prints one RMS value per
second.

| Symptom                        | Likely cause                                             |
|--------------------------------|----------------------------------------------------------|
| `MIC INIT FAILED`              | I2S port 0 already in use, or wrong pins.                |
| RMS stuck at 0                 | Sense expansion board not seated (the mic is on it).     |
| RMS doesn't change when you speak | Clock and data pins swapped. Check GPIO42/41.         |

### Subsystem 3: camera live stream (XIAO → Wi-Fi → browser)

1. Copy `include/secrets.example.h` to `include/secrets.h` and fill in the network details. The
   XIAO only supports **2.4 GHz** Wi-Fi.
2. Set `APP_MODE_CAMERA_STREAM` in `src/main.cpp`, then flash.
3. Open the serial monitor. It prints a Wi-Fi scan (and warns if your SSID isn't visible), then
   `Connected! Open http://<ip>/`.
4. From a device on the same network, open `http://<ip>/` for the viewer page, or
   `http://<ip>/stream` for the raw MJPEG stream.
5. Pass condition: live, upright video at QVGA (320×240) that keeps updating.

| Symptom                              | Likely cause                                                      |
|--------------------------------------|-------------------------------------------------------------------|
| `Camera init failed with error 0x…`  | Sense board not seated on the XIAO, or PSRAM not enabled (`qio_opi` in `platformio.ini`). |
| SSID "NOT in the scan results"       | Network is 5 GHz-only, out of range, or the SSID doesn't match exactly. |
| Connects, but the page won't load    | Browser is on a different network, or client isolation on a phone hotspot. |
| Image is upside down                 | Change `set_vflip` in `configureCamera()`.                        |
| Stream stutters                      | Weak signal. Check RSSI in the scan, or move closer.              |

Only one browser tab can view `/stream` at a time. The HTTP server handles one stream client.
The UNO Q dashboard will later re-serve the stream to everyone else.

### Subsystem 4: XIAO ↔ UNO Q I²C link

**Wiring.** Keep the wires short.

```
XIAO D4 / GPIO5 (SDA) ──────┬─── UNO Q A4 (SDA, PC1)
XIAO D5 / GPIO6 (SCL) ────┬─┼─── UNO Q A5 (SCL, PC0)
XIAO GND ─────────────────┼─┼─── UNO Q GND
                         4.7k 4.7k  → 3.3 V   (one pair of pull-ups for the whole bus)
```

I²C needs pull-ups. Neither the XIAO's D4/D5 nor the UNO Q's A4/A5 are documented as having
them, so add **4.7 kΩ from SDA to 3.3 V and from SCL to 3.3 V**. Both boards use 3.3 V logic,
so no level shifter is needed.

**Which `Wire` object on the UNO Q?** The bus is set in the UNO Q Zephyr core's board file
(`arduino:zephyr` 1.0.0, `variants/arduino_uno_q_stm32u585xx`):

| Object  | Peripheral | Pins                          |
|---------|------------|-------------------------------|
| `Wire`  | i2c2       | D20 (SDA) / D21 (SCL) header  |
| `Wire1` | i2c4       | Qwiic connector               |
| `Wire2` | i2c3       | **A4 = PC1 (SDA), A5 = PC0 (SCL)** ← Dorbel |

Using `Wire` would drive D20/D21, and the XIAO on A4/A5 would never answer. If you rewire to
D20/D21, change `#define DORBEL_WIRE Wire2` to `Wire` in the master sketch.

**XIAO (slave).** Set `APP_MODE_I2C_SLAVE`, then flash. The serial output should show
`XIAO I2C slave ready at 0x08`, followed by a `[stats]` line every 5 s.

**UNO Q (master), step 10.** Open `unoq/i2c_master_test` in Arduino App Lab and run it. If you
can't open the folder directly, create a new app and paste in `sketch/sketch.ino`. All UNO Q
sketches use `Arduino_RouterBridge` (declared in each `sketch/sketch.yaml`) and print with
`Monitor`, because on the UNO Q MCU, `Serial` only reaches the D0/D1 UART pins. These sketches
are adapted from the reference ones in two ways: `Wire` → `Wire2` (A4/A5) and `Serial` →
`Monitor`.

The sketch sends `PING` once, then repeats: read status, wait 1 s, send `RING`, wait 3 s.

**Pass condition.** UNO Q (App Lab console):

```
UNO Q I2C master ready
Command 1 result = 0
XIAO status: 0x07
Command 2 result = 0
```

XIAO (serial monitor): `RING COMMAND RECEIVED`, and the speaker plays the chime. In
`APP_MODE_I2C_SLAVE` the XIAO starts the MAX98357A, plays the chime on every RING, and reports
`STATUS_AUDIO` only if the speaker started. Status `0x03` therefore means the link works but the
speaker failed to start.

| Symptom                              | Likely cause                                         |
|--------------------------------------|------------------------------------------------------|
| `result = 2` (address NACK)          | Missing GND or pull-ups, SDA/SCL swapped, XIAO not in `APP_MODE_I2C_SLAVE`, or `Wire` used instead of `Wire2`. |
| `result = 0` but `No XIAO response`  | Slave too slow to answer. Keep 100 kHz.              |
| Intermittent errors                  | Long or loose wires, pull-ups missing or too weak.   |
| No output in App Lab                 | Output went to `Serial` instead of `Monitor`, or the delay after `Monitor.begin()` was removed. |

To compile all UNO Q sketches from the command line:

```bash
for s in i2c_master_test io_test doorbell_ring dorbel_bridge; do
  arduino-cli compile --fqbn arduino:zephyr:unoq unoq/$s/sketch
done
```

### Subsystem 5: button + RGB (step 11)

There is no door sensor in V1.

```
UNO Q D2 ── button ── GND          (INPUT_PULLUP, pressed = LOW)
UNO Q D3 ──220Ω── R ┐
UNO Q D5 ──220Ω── G ├── RGB LED, common cathode → GND
UNO Q D6 ──220Ω── B ┘
```

Run `unoq/io_test`.

**Pass condition:** the LED is **green** after boot. Holding the button turns it **red** and
prints `DOORBELL PRESSED`.

### Button → I²C → XIAO → speaker (step 12)

```
BUTTON → UNO Q → I²C RING (0x02) → XIAO → MAX98357A → SPEAKER
```

1. Flash the XIAO in `APP_MODE_I2C_SLAVE`. The speaker must be wired as in Subsystem 1.
2. Run `unoq/doorbell_ring` on the UNO Q.

The button is debounced and triggers once per press. Presses within 1.5 s of a ring are ignored
while the chime plays. The UNO Q polls the XIAO status every second without blocking.

| LED   | Meaning                                       |
|-------|-----------------------------------------------|
| Green | Idle, XIAO answering on I²C                   |
| Red   | Ring just sent (1 s)                          |
| Blue  | XIAO not answering. Check wiring or XIAO mode. |

**Pass condition:** each press turns the LED red for 1 s, prints `DOORBELL PRESSED -> RING sent`,
and the speaker plays the chime. Unplugging the XIAO turns the LED blue.

### UNO Q STM32 ↔ Linux Bridge (steps 13–14)

`unoq/dorbel_bridge` is step 12 plus a small RPC API that the STM32 exposes to Python on the
Linux side with `Bridge.provide_safe()`:

| RPC                    | Returns                                                     |
|------------------------|-------------------------------------------------------------|
| `get_doorbell_state()` | `1` if the button was pressed since the last `clear_event()` |
| `get_xiao_status()`    | last XIAO status byte, `-1` if the XIAO isn't answering     |
| `clear_event()`        | clears the doorbell flag, returns `1`                       |

`provide_safe` handlers run between `loop()` iterations on the MCU. So `loop()` never blocks for
long, otherwise `Bridge.call()` from Python stalls.

`python/main.py` polls every 0.5 s and prints the state. When it sees a press, it prints
`RING event received from MCU` and calls `clear_event()`, so each press is reported once.

**Pass condition.** The App Lab Python console shows `doorbell=0 xiao=0x07` while idle. Pressing
the button gives:

```
doorbell=1 xiao=0x07
RING event received from MCU
```

At the same time the chime plays and the LED flashes red. At this point both paths are proven:

```
Physical:  button → STM32 → I²C → XIAO → speaker
Software:  STM32 → Bridge RPC → Linux Python
```

The Linux side is where the Flask dashboard and Telegram notifications from Trillo will connect.
