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
       ┌────────┼─────────┐           ┌───────┼──────────┐
       ▼        ▼         ▼           ▼       ▼          ▼
    CAMERA     MIC      I2S OUT     BUTTON   RGB       DOOR SENSOR
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
| I²C SDA (slave, `0x08`) | D4       | GPIO5  | UNO Q SDA           |
| I²C SCL                 | D5       | GPIO6  | UNO Q SCL           |
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
| Reed switch   | D7        | to GND, `INPUT_PULLUP`                           |
| I²C SDA       | **D18**   | to XIAO D4. This build uses D18/D19, not D20/D21 |
| I²C SCL       | **D19**   | to XIAO D5                                       |

The RGB LED is assumed to be common-cathode, with the common pin to GND. If yours is
common-anode, invert the LED logic.

---

## I²C protocol

- **UNO Q = master** (`Wire.begin()`). This is the configuration the UNO Q docs demonstrate,
  so it is the lower-risk choice.
- **XIAO = slave at `0x08`**, using `Wire.onReceive()` / `Wire.onRequest()`.

| Direction    | Message        | Value  |
|--------------|----------------|--------|
| UNO Q → XIAO | `CMD_PING`     | `0x01` |
| UNO Q → XIAO | `CMD_RING`     | `0x02` |
| UNO Q → XIAO | `CMD_TALK_ON`  | `0x03` |
| UNO Q → XIAO | `CMD_TALK_OFF` | `0x04` |
| XIAO → UNO Q | status flags   | `STATUS_WIFI`, `STATUS_AUDIO`, `STATUS_CAMERA`, `STATUS_TALK` |

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

## Code base (XIAO firmware)

This is a PlatformIO project for the XIAO ESP32S3 Sense.

```
platformio.ini            board, PSRAM (qio_opi), huge_app partition, USB-CDC serial
include/
  camera_pins.h           OV2640 pin map for the Sense board
  mic_capture.h           mic test API
  speaker_test.h          speaker test API
  mic_level.h             live mic level meter API
  secrets.example.h       Wi-Fi credentials template → copy to secrets.h (gitignored)
src/
  main.cpp                APP_MODE selector + camera setup + Wi-Fi connect
  app_httpd.cpp           MJPEG HTTP server: "/" page and "/stream"
  mic_capture.cpp         records 10 s from the PDM mic; saves /mic_test.wav if an SD card is present,
                          otherwise prints per-second RMS over serial
  mic_level.cpp           live mic RMS meter over serial (no SD card needed)
  speaker_test.cpp        MAX98357A "ding-dong" tone test over I2S
stl/                      enclosure models (base, cover, body)
```

### Selecting a subsystem test

Each subsystem is tested on its own before integration. To pick one, edit this line in
`src/main.cpp`:

```cpp
#define APP_MODE APP_MODE_MIC_LEVEL   // or APP_MODE_CAMERA_STREAM / APP_MODE_MIC_CAPTURE / APP_MODE_SPEAKER_TEST
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

| # | Subsystem                         | Board | Mode / file                                | Status |
|---|-----------------------------------|-------|--------------------------------------------|--------|
| 1 | MAX98357A speaker                 | XIAO  | `APP_MODE_SPEAKER_TEST`, `speaker_test.cpp`| Builds, needs hardware test |
| 3 | Camera MJPEG stream → browser    | XIAO  | `APP_MODE_CAMERA_STREAM`, `app_httpd.cpp`  | Builds, re-test on hardware |
| 2 | Onboard PDM mic, live RMS        | XIAO  | `APP_MODE_MIC_LEVEL`, `mic_level.cpp`      | Running on hardware, no SD card. Voice response still to confirm |
| 2b | PDM mic → WAV clip (SD optional) | XIAO  | `APP_MODE_MIC_CAPTURE`, `mic_capture.cpp`  | Builds; SD-free fallback untested |
| 2 | Wi-Fi audio (mic up, speaker down)| XIAO  | —                                          | Planned |
| 3 | UNO Q I/O: button, RGB, reed      | UNO Q | —                                          | Planned |
| 4 | I²C link: UNO Q master ↔ XIAO `0x08` | both | —                                       | Planned |
| 5 | UNO Q app: Flask dashboard + Telegram (from Trillo) | UNO Q | —                      | Planned |
| 6 | Integration + battery power       | both  | —                                          | Planned |

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
