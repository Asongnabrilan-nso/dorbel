# Dorbel

Dorbel is a private smart doorbell that does its processing at the edge. It is built on an
**Arduino UNO Q (4 GB)** as the system brain and a **Seeed XIAO ESP32S3 Sense** as the
multimedia endpoint (camera, microphone, speaker).

The application layer (Flask dashboard, video streaming, Telegram notifications, audio) follows
[Q04 Trillo](https://gitlab.com/supermoderno/q04_trillo). Dorbel does **not** use Trillo's Media
Carrier / IMX219 hardware path. Camera and audio come from the XIAO over Wi-Fi.
The speaker wiring follows
[Xiaozhi-for-XiaoESP32S3](https://github.com/TechTalkies/Xiaozhi-for-XiaoESP32S3).

**New here? Go straight to [Full setup guide](#full-setup-guide) and then
[Live demo](#live-demo).** The rest of this file is reference material: pin maps, protocol,
and per-subsystem bring-up tests.

## Contents

- [Architecture](#architecture-v1-locked) · [Pin map](#pin-map) · [I²C protocol](#ic-protocol) · [Power](#power)
- [Code base](#code-base) · [Build order and test status](#build-order-and-test-status)
- [Dorbel V1](#dorbel-v1-camera--push-to-talk--dashboard--telegram)
  - [Bill of materials](#bill-of-materials)
  - [Full setup guide](#full-setup-guide) (wiring → XIAO → UNO Q → Telegram bot → browser)
  - [Live demo](#live-demo) (pre-flight checklist, run sheet, fallbacks)
  - [Troubleshooting](#troubleshooting)
- [Finishing V1](#finishing-v1)

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
onboard microphone uses. Camera pins are listed in `xiao/include/camera_pins.h`.

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
| UNO Q → XIAO | `CMD_GET_IP`   | `0x05`. The next read returns 4 bytes, the XIAO's IPv4 `a.b.c.d` (`0.0.0.0` = no Wi-Fi) |
| XIAO → UNO Q | status byte (bit flags) | `STATUS_ALIVE` 0x01 (Wi-Fi connected in `APP_MODE_DORBEL`), `STATUS_CAMERA` 0x02, `STATUS_AUDIO` 0x04, `STATUS_TALK` 0x08 (homeowner talking) |

The UNO Q learns the XIAO's IP over I²C, so the dashboard finds the camera without any setup.

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

The repository holds two firmware targets, laid out so each tool opens its own part:

- **UNO Q:** the repo root **is** the Arduino App Lab app (`app.yaml` + `sketch/` + `python/`).
  Clone it into `~/ArduinoApps/dorbel` on the UNO Q and it shows up in App Lab as **Dorbel**.
- **XIAO ESP32S3 Sense:** a PlatformIO project in `xiao/`. Open **that folder** (not the repo
  root) in VS Code / PlatformIO.

```
app.yaml                  App Lab app metadata ("Dorbel"), exposes dashboard port 8000
sketch/                   UNO Q STM32 sketch: button, RGB, I²C master, Bridge RPC
  sketch.ino
  sketch.yaml             arduino:zephyr + Arduino_RouterBridge
python/                   Linux side, standard library only (no pip installs)
  main.py                 polls the Bridge, keeps state + event log
  dashboard.py            HTTP server on :8000 (page + JSON API)
  dashboard.html          video, status, push-to-talk, events
  users.html              enable/disable Telegram users
  notifier.py             Telegram bot: /start registration + door alerts with photo
  dorbel_config_example.py  settings template → copy to dorbel_config.py (gitignored)
xiao/                     XIAO PlatformIO project — open this folder in PlatformIO
  platformio.ini          board, PSRAM (qio_opi), huge_app partition, USB-CDC serial
  include/
    camera_pins.h         OV2640 pin map for the Sense board
    dorbel_protocol.h     I²C address, CMD_* and STATUS_* values (mirrored in UNO Q sketches)
    mic_capture.h         mic WAV capture API
    mic_level.h           live mic level meter API
    speaker_test.h        speaker API: initSpeaker(), playTone(), playChime()
    i2c_slave_test.h      I²C slave test API
    i2c_link.h            shared I²C slave: command queue, status byte, IP reply
    intercom.h            WebSocket push-to-talk intercom API
    secrets.example.h     Wi-Fi credentials template → copy to secrets.h (gitignored)
  src/
    main.cpp              APP_MODE selector, camera setup, Wi-Fi, APP_MODE_DORBEL integration
    app_httpd.cpp         HTTP servers: test-mode "/" + "/stream"; Dorbel mode :80 + :81
    intercom.cpp          ws://<xiao>/audio: mic → browser, browser → speaker, chime
    i2c_link.cpp          I²C slave at 0x08 (used by the test mode and Dorbel mode)
    speaker_test.cpp      MAX98357A over I2S port 1: tones + "ding-dong" chime
    mic_level.cpp         live mic RMS meter over serial (no SD card needed)
    mic_capture.cpp       10 s mic clip: /mic_test.wav if an SD card is present, else RMS over serial
    i2c_slave_test.cpp    I²C slave at 0x08: logs commands, answers status, plays chime on RING
unoq_tests/               earlier UNO Q step apps (each is app.yaml + sketch/ + python/)
  i2c_master_test/        step 10: PING + RING over I²C, prints XIAO status
  io_test/                step 11: button + RGB LED
  doorbell_ring/          step 12: button → I²C RING → XIAO chime
stl/                      enclosure models (base, cover, body)
```

### Running a UNO Q step test app

App Lab only lists apps that sit directly in `~/ArduinoApps/`. Because the repo root is the main
app, the step apps in `unoq_tests/` are not listed. To run one, copy it next to the main app:

```bash
cp -r ~/ArduinoApps/dorbel/unoq_tests/io_test ~/ArduinoApps/dorbel-io-test
```

It then appears in App Lab (e.g. "Dorbel IO Test"). Delete the copy when you're done. Edit the
original in `unoq_tests/` if you want the change kept in git.

### Selecting a subsystem test

Each subsystem is tested on its own before integration. To pick one, edit this line in
`xiao/src/main.cpp`:

```cpp
#define APP_MODE APP_MODE_DORBEL   // V1 (default), or _I2C_SLAVE / _SPEAKER_TEST / _MIC_LEVEL / _MIC_CAPTURE / _CAMERA_STREAM
```

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
cd xiao                      # the PlatformIO project lives here
cp include/secrets.example.h include/secrets.h   # first time only; fill in Wi-Fi
pio run                      # build
pio run -t upload            # flash (close any open serial monitor first)
pio device monitor           # 115200 baud
```

PlatformIO's CLI is at `~/.platformio/penv/Scripts/pio.exe` if `pio` is not on your PATH.

> Serial runs over native USB. `setup()` waits up to 3 s for a USB host, then starts anyway,
> so the firmware also runs on battery.

---

## Build order and test status

| Step | Subsystem                                   | Board | Code                                          | Status |
|------|---------------------------------------------|-------|-----------------------------------------------|--------|
| 1    | MAX98357A speaker                           | XIAO  | `APP_MODE_SPEAKER_TEST`                        | Builds, needs a test on the board |
| 2    | Onboard PDM mic, live RMS (no SD)           | XIAO  | `APP_MODE_MIC_LEVEL`                           | Running on the board; voice response to confirm |
| 2b   | PDM mic → WAV clip (SD optional)            | XIAO  | `APP_MODE_MIC_CAPTURE`                         | Builds; SD-free fallback untested |
| 3    | Camera MJPEG stream → browser               | XIAO  | `APP_MODE_CAMERA_STREAM`                       | Builds, re-test on the board |
| 4/10 | I²C link: UNO Q master ↔ XIAO `0x08`        | both  | `APP_MODE_I2C_SLAVE` + `unoq_tests/i2c_master_test`  | XIAO flashed and running; UNO Q compiles, needs a test on the board |
| 11   | Button + RGB LED                            | UNO Q | `unoq_tests/io_test`                                 | Compiles, needs a test on the board |
| 12   | Button → I²C RING → XIAO chime              | both  | `unoq_tests/doorbell_ring` + `APP_MODE_I2C_SLAVE`    | Compiles, needs a test on the board |
| 13–14| STM32 ↔ Linux Bridge RPC + Python poller    | UNO Q | the main **Dorbel** app (repo root)                           | Working |
| 15   | Push-to-talk intercom over Wi-Fi            | XIAO  | `APP_MODE_DORBEL` (`intercom.cpp`)             | Builds, needs a test on the board |
| 16–18| Dashboard + Telegram alerts                 | UNO Q | `python/` (main app)                           | Smoke-tested on a PC with a stub Bridge; needs a test on the board |
| 19   | V1 end-to-end demo                          | both  | see "Dorbel V1" below                          | Needs a test on the board |

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

1. Copy `xiao/include/secrets.example.h` to `xiao/include/secrets.h` and fill in the network details. The
   XIAO only supports **2.4 GHz** Wi-Fi.
2. Set `APP_MODE_CAMERA_STREAM` in `xiao/src/main.cpp`, then flash.
3. Open the serial monitor. It prints a Wi-Fi scan (and warns if your SSID isn't visible), then
   `Connected! Open http://<ip>/`.
4. From a device on the same network, open `http://<ip>/` for the viewer page, or
   `http://<ip>/stream` for the raw MJPEG stream.
5. Pass condition: live, upright video at QVGA (320×240) that keeps updating.

| Symptom                              | Likely cause                                                      |
|--------------------------------------|-------------------------------------------------------------------|
| `Camera init failed with error 0x…`  | Sense board not seated on the XIAO, or PSRAM not enabled (`qio_opi` in `xiao/platformio.ini`). |
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

**UNO Q (master), step 10.** Copy `unoq_tests/i2c_master_test` into `~/ArduinoApps/` (see
"Running a UNO Q step test app"), then open it in Arduino App Lab and run it. All UNO Q
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
arduino-cli compile --fqbn arduino:zephyr:unoq sketch
for s in i2c_master_test io_test doorbell_ring; do
  arduino-cli compile --fqbn arduino:zephyr:unoq unoq_tests/$s/sketch
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

Run `unoq_tests/io_test`.

**Pass condition:** the LED is **green** after boot. Holding the button turns it **red** and
prints `DOORBELL PRESSED`.

### Button → I²C → XIAO → speaker (step 12)

```
BUTTON → UNO Q → I²C RING (0x02) → XIAO → MAX98357A → SPEAKER
```

1. Flash the XIAO in `APP_MODE_I2C_SLAVE`. The speaker must be wired as in Subsystem 1.
2. Run `unoq_tests/doorbell_ring` on the UNO Q.

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

the main **Dorbel** app (repo root) is step 12 plus a small RPC API that the STM32 exposes to Python on the
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

---

## Dorbel V1: camera + push-to-talk + dashboard + Telegram

```
VIDEO   XIAO camera ──Wi-Fi── http://<xiao>:81/stream ──────────► browser
LISTEN  XIAO mic    ──Wi-Fi── ws://<xiao>/audio (16 kHz PCM) ───► browser
TALK    browser mic ──Wi-Fi── ws://<xiao>/audio ──► XIAO ──I2S──► MAX98357A
RING    button ─► UNO Q ─I²C RING─► XIAO chime
               └─► Bridge ─► python/main.py ─► Telegram + dashboard events
```

Video and audio go straight from the browser to the XIAO. The UNO Q serves the dashboard page
and learns the XIAO's IP over I²C (`CMD_GET_IP`). The intercom is **half duplex**: while the
homeowner holds TALK, the XIAO stops sending mic audio, so there is no feedback loop.

### XIAO endpoints (`APP_MODE_DORBEL`)

| URL                       | What                                                     |
|---------------------------|----------------------------------------------------------|
| `http://<xiao>/`          | bare camera page (the "live video" link in alerts)       |
| `http://<xiao>:81/stream` | MJPEG stream, on its own server so it never blocks audio. One viewer at a time |
| `http://<xiao>/capture`   | a single JPEG, used for the Telegram alert photo         |
| `ws://<xiao>/audio`       | intercom: binary = 16 kHz 16-bit LE mono PCM both ways; text `talk:1` / `talk:0` |

### Bill of materials

| Qty | Part | Notes |
|-----|------|-------|
| 1 | Arduino UNO Q (4 GB) | system brain, runs the Dorbel App Lab app |
| 1 | Seeed XIAO ESP32S3 **Sense** | must be the Sense version (camera + mic expansion board seated) |
| 1 | MAX98357A I2S amplifier breakout | |
| 1 | 8 Ω / 2 W speaker | across SPK+ / SPK- |
| 1 | Momentary push button | the doorbell |
| 1 | RGB LED, common cathode, + 3 × 220 Ω | status light |
| 2 | 4.7 kΩ resistors | I²C pull-ups to 3.3 V |
| — | Jumper wires, breadboard | keep I²C wires short |
| 2 | USB-C data cables | one per board (bench power and flashing) |
| — | 2.4 GHz Wi-Fi with internet | XIAO is 2.4 GHz only; UNO Q needs internet for Telegram |
| — | A phone with Telegram | receives the alerts |
| — | A laptop or Android phone with Chrome/Edge | the dashboard (TALK needs Chrome/Edge, not iOS Safari) |

### Full setup guide

Do the steps in order. Each step has a check. Don't move on until it passes.

#### Step 1: Wire everything

Follow the [Pin map](#pin-map). In short:

```
XIAO D4 (SDA) ──┬── UNO Q A4          UNO Q D2 ── button ── GND
XIAO D5 (SCL) ──┼┬─ UNO Q A5          UNO Q D3 ─220Ω─ R ┐
XIAO GND ───────┼┼─ UNO Q GND         UNO Q D5 ─220Ω─ G ├─ RGB, common → GND
              4.7k 4.7k → 3.3 V       UNO Q D6 ─220Ω─ B ┘

XIAO D8 → MAX BCLK   XIAO D3 → MAX LRC   XIAO D1 → MAX DIN
XIAO 5V → MAX VIN    XIAO GND → MAX GND  MAX SPK+ / SPK- → speaker (never SPK- to GND)
```

**Check:** the grounds of both boards are connected, and there is one pair of pull-ups on SDA/SCL.

#### Step 2: Flash the XIAO

On your PC, with [PlatformIO](https://platformio.org/) installed (VS Code extension or CLI):

```bash
cd xiao                                           # open THIS folder in PlatformIO, not the repo root
cp include/secrets.example.h include/secrets.h    # first time only
# edit include/secrets.h → WIFI_SSID / WIFI_PASSWORD of a 2.4 GHz network
pio run -t upload                                 # close any serial monitor first
pio device monitor                                # 115200 baud
```

On Windows, use `~/.platformio/penv/Scripts/pio.exe` if `pio` isn't on PATH. Make sure
`xiao/src/main.cpp` has `#define APP_MODE APP_MODE_DORBEL` (the default).

**Check:** the serial log shows `Mode: Dorbel V1 (camera + intercom + I2C link)`, then
`XIAO I2C slave ready at 0x08`, then:

```
WiFi up: camera http://192.168.x.y/  stream http://192.168.x.y:81/stream  intercom ws://192.168.x.y/audio
```

Write the IP down. Open `http://<xiao-ip>/` from a laptop on the same Wi-Fi. You should see
live video. **Close that tab afterwards**, because the stream accepts only one viewer.

#### Step 3: Put the Dorbel app on the UNO Q

The UNO Q runs Linux. Get a shell on it, either over the network
(`ssh arduino@<unoq-hostname>.local`) or over USB (`adb shell`). Then:

```bash
cd ~/ArduinoApps
git clone https://github.com/Asongnabrilan-nso/dorbel.git dorbel
cd dorbel/python
cp dorbel_config_example.py dorbel_config.py      # your local settings, gitignored
hostname -I                                       # note the LAN IP, e.g. 192.168.x.z
```

To update later, run `cd ~/ArduinoApps/dorbel && git pull`. Your `dorbel_config.py` and the
Telegram user DB are gitignored, so they survive updates.

Edit `python/dorbel_config.py` (`nano dorbel_config.py`). Set at least:

```python
DASHBOARD_URL = "http://192.168.x.z:8000/"   # the UNO Q LAN IP from `hostname -I`
```

App Lab runs the Python side in a container, so without this setting the Telegram alert could
link to an internal `172.x` address. Leave `TELEGRAM_BOT_TOKEN` empty for now (Step 4).

**Check:** open **Arduino App Lab**. The app **Dorbel 🔔** is listed. Click **Run**. App Lab
compiles and flashes `sketch/` to the STM32 and starts `python/main.py`. The Python console
shows:

```
Telegram disabled: set TELEGRAM_BOT_TOKEN in python/dorbel_config.py
Dorbel dashboard: http://192.168.x.z:8000/
[event] System started
[event] MCU connected
[event] XIAO online
[event] Camera at 192.168.x.y
```

The RGB LED is **green**. If it's **blue**, the XIAO isn't answering on I²C. See
[Subsystem 4](#subsystem-4-xiao--uno-q-ic-link).

#### Step 4: Create and connect the Telegram bot

1. In Telegram, open **@BotFather** (the official one, with the blue check) and send `/newbot`.
2. Give it a display name (e.g. `Dorbel Front Door`), then a username that ends in `bot`
   (e.g. `dorbel_frontdoor_bot`). It must be unique across Telegram.
3. BotFather replies with a **token** like `123456789:AAH...`. Treat it like a password: it
   gives full control of the bot. Never commit it. It only goes in the gitignored
   `dorbel_config.py`.
4. Optional polish in BotFather:
   - `/setcommands` → pick your bot → send `start - Register for door alerts`
   - `/setdescription` → `Dorbel smart doorbell. Send /start to register.`
   - `/setuserpic` → upload a doorbell icon
5. Check the token from the UNO Q shell (this also confirms the board has internet):

   ```bash
   curl -s https://api.telegram.org/bot<TOKEN>/getMe
   # → {"ok":true,"result":{"id":...,"is_bot":true,"username":"dorbel_frontdoor_bot",...}}
   ```

6. Put the token in `python/dorbel_config.py`:

   ```python
   TELEGRAM_BOT_TOKEN = "123456789:AAH..."
   ```

7. **Stop and Run** the app again in App Lab. The `Telegram disabled` line is gone.
8. **Register each homeowner.** On their phone, open `t.me/<bot_username>` and tap **Start**
   (or send `/start`). The bot replies *"Welcome to Dorbel 🔔 … You are registered."* and the
   dashboard log shows `Telegram user registered: <name>`.
9. **Enable them.** Open `http://<unoq-ip>:8000/users` (the **Telegram users** link at the
   bottom of the dashboard) and click the toggle next to each name. New users start
   **disabled**, so strangers who find the bot get nothing.

How it works (`python/notifier.py`): the bot long-polls Telegram's `getUpdates`, so it needs
no public IP, port forwarding or webhook. Users live in SQLite (`python/dorbel_users.db`,
max 10). Each ring sends enabled users the `/capture` photo from the XIAO, with the alert text
as the caption. If the photo can't be fetched within 3 s, the text goes out alone. Alerts are
limited to one every 10 s.

**Check:** press the doorbell. Within a few seconds the enabled phone receives a photo with
`🔔 DORBEL ALERT … Someone is at the door.`, and the dashboard log shows
`Telegram alert sent to 1/1`.

#### Step 5: Prepare the browser for push-to-talk

Browsers only allow the microphone (`getUserMedia`) on `https://` or `localhost`. The
dashboard is plain `http://`, so on the demo laptop / Android phone:

1. Open `chrome://flags/#unsafely-treat-insecure-origin-as-secure` (Edge: `edge://flags/...`).
2. Add `http://<unoq-ip>:8000`, set it to **Enabled**, then click **Relaunch**.

Listening works without this. Only TALK needs it. iOS Safari has no equivalent.

**Check:** open `http://<unoq-ip>:8000/`. SYSTEM shows `● ONLINE`, video plays, and holding
**HOLD TO TALK** asks for mic permission (allow it) with no warning under the button.

You're ready to demo.

### Live demo

#### Pre-flight checklist (do this 30 min before)

- [ ] All devices are on the **same 2.4 GHz network**: XIAO, UNO Q, the demo laptop and the
      phone (the phone needs internet for Telegram). The venue's Wi-Fi often has client
      isolation or captive portals. Bring your own travel router or phone hotspot (on iPhone:
      *Maximize Compatibility* = 2.4 GHz) and set its SSID in `secrets.h` and on the UNO Q
      ahead of time.
- [ ] Power both boards. Power-up order doesn't matter. The UNO Q picks up the XIAO within
      ~5 s.
- [ ] In App Lab, run **Dorbel**. LED is green, and the console shows `Camera at <ip>`.
- [ ] Dashboard open on the demo laptop. SYSTEM `● ONLINE`, live video.
      **No other tab or device has the stream open.**
- [ ] Telegram open on the phone. The user is **enabled** on `/users`.
- [ ] Do one full dry run: ring, then listen, then talk. Wait **10 s** after the dry-run ring
      before the real one, because of the alert rate limit.
- [ ] Speaker volume is OK (adjust `MIC_GAIN` / amplitude beforehand, not live).
- [ ] Phone notifications are on and not muted. Mirror the phone screen if the room is big.

#### Run sheet (~5 minutes)

| # | Say | Do | Audience sees |
|---|-----|----|---------------|
| 1 | "Dorbel is a private, edge-processed smart doorbell: a UNO Q as the brain, a XIAO as eyes, ears and mouth." | Show the architecture diagram | I²C = control, Wi-Fi = media |
| 2 | "This is the homeowner's dashboard, served by the UNO Q itself, with no cloud." | Show the dashboard | SYSTEM ● ONLINE, live camera, event log |
| 3 | "A visitor arrives…" | **Press the doorbell** | Chime from the door speaker, LED red, DOORBELL ● PRESSED, VISITOR ● PRESENT |
| 4 | "…and the homeowner gets a photo, wherever they are." | Hold up / mirror the phone | Telegram photo + `🔔 DORBEL ALERT` |
| 5 | "I can hear who's there…" | Click **🔊 Listen to door**. Have a helper speak at the door | Door audio through the laptop |
| 6 | "…and answer them." | **Hold TALK** (or space bar) and speak | Voice from the door speaker. INTERCOM ● TALKING, event `Intercom started` |
| 7 | "Only people I approve get alerts." | Open **Telegram users**, show the toggle | Access control without accounts or passwords |
| 8 | "Video and audio stay on the local network. Only the alert leaves the house." | Back to the dashboard | Event log of the whole demo |

Tips: the intercom is half-duplex, so pause after releasing TALK before the visitor answers.
Keep the laptop away from the door speaker to avoid echo when Listen is on.

#### If something goes wrong on stage

| Problem | Fast recovery |
|---------|---------------|
| Video frozen / "only one viewer" | Close other tabs/devices viewing the stream. Reload the dashboard. |
| No Telegram message | Wait 10 s (rate limit) and ring again. Otherwise show the dashboard event log line `Telegram alert sent…` / `No enabled Telegram users`. |
| TALK blocked | Carry on with Listen only, and explain the https/secure-context limitation. |
| SYSTEM `XIAO OFFLINE`, LED blue | Re-seat the I²C/GND wires, or reset the XIAO (it rejoins in ~10 s). |
| Nothing at all | Re-run the app in App Lab (~30 s). Keep a screen recording of a good run as a last resort. |

### Troubleshooting

| Symptom                               | Likely cause                                               |
|---------------------------------------|------------------------------------------------------------|
| Video says "only one viewer"          | Another tab or device already has `:81/stream` open.       |
| Dashboard unreachable from the phone  | Not on the same network, client isolation, or App Lab didn't publish port 8000. Check `ports:` in `app.yaml`. |
| SYSTEM `MCU OFFLINE`                  | The STM32 sketch isn't running or hasn't registered its RPCs yet. Re-run the app. |
| SYSTEM `XIAO OFFLINE`, LED blue       | I²C wiring/pull-ups/GND, or the XIAO isn't in `APP_MODE_DORBEL`. |
| SYSTEM `XIAO NO WI-FI`                | Wrong SSID/password in `xiao/include/secrets.h`, or a 5 GHz-only network. |
| Telegram link points to a 172.x address | App Lab runs Python in a container. Set `DASHBOARD_URL` in `dorbel_config.py`. |
| `Telegram disabled` in the console    | `TELEGRAM_BOT_TOKEN` empty, or `dorbel_config.py` not in `python/` next to `main.py`. |
| `Telegram polling error: HTTP Error 401` | Wrong token. Copy it again from BotFather (`/mybots` → API Token). |
| `Telegram polling error: HTTP Error 409` | Another copy of the app (e.g. on your PC) is polling the same bot, or a webhook is set. Stop the other copy, or run `curl https://api.telegram.org/bot<TOKEN>/deleteWebhook`. |
| `Telegram polling error: <urlopen error ...>` | The UNO Q has no internet or DNS. Test with `curl https://api.telegram.org`. |
| `/start` gets no reply                | App not running, or token error (see above). |
| Event `No enabled Telegram users to alert` | Enable the user on the `/users` page. |
| Alert arrives without a photo         | The XIAO `/capture` took > 3 s or the IP is unknown. Check `Camera at <ip>` in the log. |
| Talk audio choppy                     | Weak Wi-Fi. The XIAO buffers 100 ms; check RSSI in the serial scan. |
| Door audio too quiet or loud          | `MIC_GAIN` in `xiao/src/intercom.cpp` (default 8).          |
| No camera IP on the dashboard         | Old UNO Q sketch without `get_xiao_ip`, or set `XIAO_HOST` in `dorbel_config.py`. |

V1 has no door sensor, so DOOR shows `NO SENSOR` and the alert leaves out the door state.
VISITOR is inferred: PRESENT for 2 minutes after a ring or a talk.

---

## Finishing V1

V1 is code-complete: the XIAO firmware builds, and the UNO Q Python app has been checked on a
PC with a stubbed Bridge (ring → event → `/api/state`, pages served). What's left is hardware
validation and packaging:

- [ ] Run steps 1–12 in [Build order](#build-order-and-test-status) on the real boards, and
      update the Status column as each one passes.
- [ ] Full [setup guide](#full-setup-guide) on the bench, including the Telegram alert with
      photo.
- [ ] Two clean [demo](#live-demo) dry runs back to back.
- [ ] Tune `MIC_GAIN` and the speaker `AMPLITUDE` for the enclosure.
- [ ] Print the enclosure (`stl/`: base, cover, body, wall mount) and test-fit the button, LED,
      speaker and camera opening.
- [ ] Move from bench USB power to the battery + 5 V buck described in [Power](#power).
- [ ] Record a short video of the demo for the project write-up.

Ideas for after V1: a door reed switch (DOOR state), HTTPS on the dashboard so TALK works on
every browser including iOS, motion-triggered alerts from the camera, and Telegram inline
buttons ("Talk now" / "Ignore").
