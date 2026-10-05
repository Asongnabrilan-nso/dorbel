# Dorbel: an AI smart doorbell on the Arduino UNO Q

<p align="center">
  <img src="media/product/Dorbel3.png" width="70%" alt="Dorbel doorbell with the ring lit while the button is pressed">
</p>

Dorbel is a private video doorbell that runs all of its processing **at the edge**. Press the
button and the door chimes. Your phone gets a Telegram photo, and a dashboard shows live video
with two-way audio. An AI model running on the board spots people at the door, even when they
don't ring.

- **Brain:** Arduino UNO Q (4 GB), running an **Arduino App Lab** app
- **Eyes, ears and mouth:** Seeed XIAO ESP32S3 Sense (camera + mic) with a MAX98357A speaker amp
- **AI:** App Lab **Video Object Detection** brick, YoloX nano model, `person` class
- **Alerts:** App Lab **Telegram Bot** brick
- **Privacy:** video and audio stay on your Wi-Fi. Only alerts leave the house.

This guide builds Dorbel in nine steps. Each one ends with a check, so don't move on until it
passes. Pin tables, the I²C protocol, subsystem tests and full troubleshooting are in
[`docs/REFERENCE.md`](docs/REFERENCE.md).

## Contents

1. [How it works](#1-how-it-works)
2. [Gather the parts](#2-gather-the-parts)
3. [Print the enclosure](#3-print-the-enclosure)
4. [Flash the XIAO](#4-flash-the-xiao)
5. [Wire the UNO Q](#5-wire-the-uno-q)
6. [Create the App Lab app](#6-create-the-app-lab-app)
7. [Add AI person detection](#7-add-ai-person-detection)
8. [Add the Telegram Bot brick](#8-add-the-telegram-bot-brick)
9. [Assemble, mount and test](#9-assemble-mount-and-test)
- [Troubleshooting](#troubleshooting) · [How AI was used in this project](#how-ai-was-used-in-this-project) · [Project files](#project-files)

---

## 1. How it works

<p align="center">
  <img src="media/workflow/Screenshot%202026-10-04%20225118.png" width="49%" alt="Dorbel block diagram">
  <img src="media/workflow/Screenshot%202026-10-04%20225259.png" width="49%" alt="Dorbel board responsibilities">
</p>

The **UNO Q** has two processors, and an App Lab app uses both:

| UNO Q part | Runs | Dorbel uses it for |
|---|---|---|
| **MCU**: STM32U585 | the app's **sketch** (`sketch/sketch.ino`) | button, RGB LED, I²C link to the XIAO |
| **MPU**: Qualcomm QRB2210, Debian Linux | the app's **Python** (`python/`) and its **bricks** (Docker containers) | dashboard, AI, Telegram |

The two sides talk over the **Bridge** (`Arduino_RouterBridge`): the sketch publishes functions
with `Bridge.provide_safe()` and Python calls them with `Bridge.call()`.

```
RING    button ─► UNO Q MCU ─I²C─► XIAO ─► chime on the door speaker
                       └─Bridge─► Python ─► Telegram Bot brick ─► 📱 photo + alert

VIDEO   XIAO camera ─Wi-Fi─► CameraHub (Python) ─┬─► dashboard (live video, any number of viewers)
                                                 └─► Video Object Detection brick (YoloX nano)
                                                        └─► "person" ─► dashboard + 📱 alert

AUDIO   browser mic ⇄ UNO Q dashboard ⇄ Wi-Fi ⇄ XIAO mic / speaker   (push-to-talk)
```

The rule: **I²C carries control and status, Wi-Fi carries media.** The XIAO reports its IP over
I²C, so the UNO Q finds the camera with no setup.

---

## 2. Gather the parts

<p align="center">
  <img src="media/electronics/IMG_20261004_082455.jpg" width="40%" alt="All Dorbel electronics laid out">
</p>

| Qty | Part | Role |
|---|---|---|
| 1 | Arduino UNO Q (4 GB) | system brain, runs the App Lab app |
| 1 | Seeed XIAO ESP32S3 **Sense** (with camera + mic board) | camera, microphone, Wi-Fi media |
| 1 | MAX98357A I2S amplifier | drives the speaker |
| 1 | 8 Ω / 2 W speaker | chime + homeowner's voice |
| 1 | Momentary push button | the doorbell |
| 1 | RGB LED (common cathode) + 3 × 220 Ω, or an RGB module | status light |
| 2 | 4.7 kΩ resistors | I²C pull-ups |
| — | Jumper wires, 2 × USB-C data cables, PLA filament | |
| — | 2.4 GHz Wi-Fi with internet, a phone with Telegram | |

<p align="center">
  <img src="media/electronics/IMG_20261004_082505.jpg" width="32%" alt="Arduino UNO Q">
  <img src="media/electronics/IMG_20261004_082525.jpg" width="32%" alt="XIAO ESP32S3 Sense with camera and antenna">
  <img src="media/electronics/IMG_20261004_082540.jpg" width="32%" alt="MAX98357A amplifier">
</p>
<p align="center">
  <img src="media/electronics/IMG_20261004_082533.jpg" width="32%" alt="8 ohm speaker">
  <img src="media/electronics/IMG_20261004_082545.jpg" width="32%" alt="Push button">
  <img src="media/electronics/IMG_20261004_082553.jpg" width="32%" alt="RGB LED module">
</p>
<p align="center"><sub>UNO Q · XIAO ESP32S3 Sense · MAX98357A · speaker · push button · RGB LED</sub></p>

**Software:** [Arduino App Lab](https://www.arduino.cc/en/software/) (preinstalled on the UNO Q),
[PlatformIO](https://platformio.org/) on your PC for the XIAO, and Telegram on your phone.

---

## 3. Print the enclosure

<p align="center">
  <img src="media/design/Screenshot%202026-10-03%20152300.png" width="45%" alt="Enclosure CAD design">
</p>

Print the four parts in `stl/`:

| File | Part |
|---|---|
| `stl/Dorbel-Cover.3mf` | front: camera hole, light-ring button, logo |
| `stl/Dorbel-Base.3mf` | back shell that holds the electronics |
| `stl/Body2.stl` | body |
| `stl/wall mount.stl` | wall plate the doorbell slides onto |

<p align="center">
  <img src="media/assembly/IMG_20261004_082630.jpg" width="32%" alt="Printed front cover">
  <img src="media/assembly/IMG_20261004_082622.jpg" width="32%" alt="Inside of the front cover">
  <img src="media/assembly/IMG_20261004_082613.jpg" width="32%" alt="Inside of the base">
</p>

**Check:** the button sits freely in the ring, and the camera lens lines up with the front hole.

---

## 4. Flash the XIAO

The XIAO is a **PlatformIO** project in `xiao/`. Open **that folder**, not the repo root.

**4.1 Wire the speaker to the XIAO**

| XIAO | MAX98357A |
|---|---|
| D8 (GPIO7) | BCLK |
| D3 (GPIO4) | LRC |
| D1 (GPIO2) | DIN |
| 5V | VIN |
| GND | GND |

Connect the speaker between **SPK+ and SPK-**. Never connect SPK- to GND.

**4.2 Set Wi-Fi and flash**

```bash
cd xiao
cp include/secrets.example.h include/secrets.h   # put your 2.4 GHz SSID + password in it
pio run -t upload
pio device monitor                               # 115200 baud
```

`xiao/src/main.cpp` must have `#define APP_MODE APP_MODE_DORBEL` (the default). The other
`APP_MODE_*` values are one-subsystem tests (speaker, mic, camera, I²C). See the
[reference](docs/REFERENCE.md#build-order-and-test-status).

<p align="center">
  <img src="media/software/firmware/PlatformIO/mic_test.png" width="80%" alt="PlatformIO serial monitor during the mic test">
</p>

**Check:** the serial log ends with
`WiFi up: camera http://192.168.x.y/  stream http://192.168.x.y:81/stream  intercom ws://192.168.x.y/audio`.
Open `http://192.168.x.y/` on your PC to see the camera, then **close the tab**. The XIAO streams
to one client only, and that slot will belong to the UNO Q.

> The XIAO and the UNO Q **must be on the same 2.4 GHz network**. This one rule breaks
> video, talk and photos when missed.

---

## 5. Wire the UNO Q

| UNO Q | Connects to | Notes |
|---|---|---|
| D2 | push button → GND | `INPUT_PULLUP` |
| D3 / D5 / D6 | RGB red / green / blue via 220 Ω | common cathode → GND |
| **A4** (SDA) | XIAO D4 | `Wire2` in the sketch, *not* the D20/D21 header |
| **A5** (SCL) | XIAO D5 | |
| GND | XIAO GND | common ground is required |
| 3.3 V | 4.7 kΩ to SDA, 4.7 kΩ to SCL | one pair of pull-ups for the bus |

The UNO Q is the I²C **master**. The XIAO is the **slave at `0x08`**.

<p align="center">
  <img src="media/test/IMG_20261004_112132.jpg" width="49%" alt="UNO Q and XIAO wired on the bench">
  <img src="media/test/IMG_20261004_112136.jpg" width="49%" alt="Bench wiring, second view">
</p>

**Check:** after step 6, the RGB LED tells you the link state.

<p align="center">
  <img src="media/test/IMG_20261004_115957.jpg" width="49%" alt="Blue LED: XIAO not answering">
  <img src="media/test/IMG_20261004_120010.jpg" width="49%" alt="Green LED: idle and linked">
</p>
<p align="center"><sub>Blue: XIAO not answering on I²C · Green: idle, link OK · Red: ring sent</sub></p>

---

## 6. Create the App Lab app

In App Lab, an **app** is a folder in `~/ArduinoApps/` that contains:

```
app.yaml      name, icon, published ports and the bricks the app uses
sketch/       runs on the MCU
python/       runs on Linux, main.py is the entry point
```

This repository **is** that folder. On the UNO Q (desktop terminal, `ssh arduino@<board>.local`
or `adb shell`):

```bash
cd ~/ArduinoApps
git clone https://github.com/Asongnabrilan-nso/dorbel.git dorbel
cd dorbel/python
cp dorbel_config_example.py dorbel_config.py      # local settings, gitignored
hostname -I                                       # the UNO Q's LAN IP
```

In `python/dorbel_config.py`, set the dashboard address that alerts will link to:

```python
DASHBOARD_URL = "https://192.168.x.z:8443/"      # the UNO Q IP from hostname -I
```

Open **Arduino App Lab**. **Dorbel 🔔** shows under *My Apps*. Click **Run**. App Lab:

1. compiles `sketch/` for the MCU and flashes it,
2. starts the app's **bricks** (Docker containers),
3. starts `python/main.py`, with its output in the App Lab **console**.

**The sketch** (`sketch/sketch.ino`) debounces the button, sends `RING` to the XIAO over I²C,
drives the LED, and publishes four functions over the Bridge:

| Bridge function | Returns |
|---|---|
| `get_doorbell_state` | `1` if the button was pressed since the last clear |
| `clear_event` | clears the press |
| `get_xiao_status` | XIAO status bits, `-1` if it isn't answering |
| `get_xiao_ip` | XIAO IP, packed in an int |

**The Python side** (`python/main.py`) polls those four times a second and runs the rest.

**Check:** the console shows

```
Dorbel dashboard: https://192.168.x.z:8443/
[event] System started
[event] MCU connected
[event] XIAO online
[event] Camera at 192.168.x.y
[event] Camera reachable at 192.168.x.y
```

and pressing the button plays the chime at the door. Open `https://<unoq-ip>:8443/` and accept
the self-signed certificate once. You see live video, status and the push-to-talk button.

<p align="center">
  <img src="media/software/telegram/Screenshot%202026-10-05%20100313.png" width="49%" alt="Dashboard: live video and status">
  <img src="media/software/telegram/Screenshot%202026-10-05%20100332.png" width="49%" alt="Dashboard: hold to talk and event log">
</p>

The dashboard runs on HTTPS because browsers only allow the microphone on secure pages.
**Hold to talk** sends your voice to the door speaker. **Listen to door** plays the XIAO mic.

---

## 7. Add AI person detection

This step adds the **Video Object Detection** brick. It runs the **YoloX nano** model (80 COCO
classes, including `person`) on the UNO Q itself, so frames never leave the board.

**7.1 Add the brick.** In App Lab, add **Video Object Detection** from the app's **Bricks**
panel, and keep the model *General purpose object detection – YoloX nano*. In `app.yaml` it
looks like this:

```yaml
bricks:
- arduino:video_object_detection:
    model: yolox-object-detection
    devices:
    - remote_camera_0
```

The brick expects a USB camera by default. `remote_camera_0` tells App Lab that the camera comes
from the network instead, like the brick's smartphone-camera example.

**7.2 Feed it the XIAO camera.** The XIAO serves one stream client at a time, so the dashboard
and the AI can't each open their own. `python/camera_hub.py` reads the stream **once** and shares
it:

- `CameraHub` keeps the newest JPEG and hands it to every dashboard viewer.
- `XiaoCamera` is an App Lab `BaseCamera`. It gives the brick the same frames, as if a local
  camera were plugged in.

```python
camera = CameraHub(current_xiao_ip)
detector = VideoObjectDetection(camera=XiaoCamera(camera), confidence=0.5, camera_preview=True)
```

**7.3 React to people.** `python/vision.py` registers `on_detect_all()`, keeps only `person`,
draws the boxes with App Lab's `draw_bounding_boxes()` helper, and fires once each time someone
**arrives**:

```python
def on_detect_all(detections: dict, frame: bytes = None):
    people = detections.get("person", [])   # [{"confidence": 0.87, "bounding_box_xyxy": (...)}]
```

On arrival, Dorbel logs `AI: person detected at the door`, sets VISITOR to PRESENT, and sends a
Telegram photo (step 8).

**Settings** in `python/dorbel_config.py`:

```python
PERSON_DETECTION = True        # run the AI at all
PERSON_CONFIDENCE = 0.5        # raise if shadows count as people, lower if people are missed
PERSON_ALERTS = True           # Telegram photo when a person arrives
PERSON_ALERT_INTERVAL_S = 120  # at most one person alert per 2 minutes
```

<p align="center">
  <img src="media/software/dashboard/dashboard-ai.png" width="45%" alt="Dashboard with the AI person detection panel">
</p>

**Check:** **Run** the app again. The dashboard's **AI PERSON DETECTION** panel shows
`● RUNNING · YoloX nano (COCO)`. Step in front of the camera:

- a `👤 PERSON 87%` badge appears on the live video,
- the panel shows the frame with your bounding box, the count and the confidence,
- the event log shows `AI: person detected at the door`.

To watch the model itself, open the brick's own preview at `http://<unoq-ip>:4912`.

---

## 8. Add the Telegram Bot brick

**8.1 Create the bot.** In Telegram, open **@BotFather**, send `/newbot`, pick a name and a
username ending in `bot`. Copy the **token** (`123456789:AAH...`). It works like a password.

<p align="center">
  <img src="media/software/telegram/photo_2026-10-05_09-47-16%20(2).jpg" width="30%" alt="The Dorbel bot's start page in Telegram">
</p>

**8.2 Add the brick.** In App Lab, add **Telegram Bot** from the **Bricks** panel. The brick
requires a `TELEGRAM_BOT_TOKEN` variable. This repo is public, so `app.yaml` only holds a placeholder:

```yaml
- arduino:telegram_bot:
    variables:
      TELEGRAM_BOT_TOKEN: set-in-dorbel_config
```

Put the real token in the gitignored `python/dorbel_config.py`:

```python
TELEGRAM_BOT_TOKEN = "123456789:AAH..."
```

(For a private copy, you can paste the token in **Brick Configuration** instead.)

**8.3 What the bot does** (`python/notifier.py`):

```python
bot = TelegramBot(token=token)
bot.add_command("start",  register,   "Register for door alerts")
bot.add_command("photo",  send_photo, "Photo from the door camera")
bot.add_command("status", status,     "Dorbel system status")
bot.add_command("help",   help,       "What this bot can do")
```

| Event | Telegram message |
|---|---|
| Doorbell pressed | `🔔 DORBEL ALERT` + 1600×1200 photo + dashboard link |
| AI sees a person arrive | `👤 Person at the door` + photo with the bounding box |
| `/photo` | a fresh full-resolution photo |
| `/status` | MCU, XIAO, camera, AI, person, last seen |

The brick long-polls Telegram, so you don't need a public IP, port forwarding or a webhook.

**8.4 Register and approve users.** Each homeowner opens the bot and taps **Start**. They start
**disabled**, so strangers who find the bot get nothing. Enable them on the dashboard's
**Telegram users** page (`https://<unoq-ip>:8443/users`).

<p align="center">
  <img src="media/software/telegram/Screenshot%202026-10-05%20100348.png" width="70%" alt="Telegram users page with alerts enabled">
</p>

**Check:** **Run** the app. The console shows `Telegram bot initialized successfully`. Press the
doorbell, and the enabled phone gets the photo alert within a few seconds:

<p align="center">
  <img src="media/software/telegram/photo_2026-10-05_09-47-15.jpg" width="30%" alt="Telegram ring alert">
  <img src="media/software/telegram/photo_2026-10-05_09-47-16.jpg" width="30%" alt="Telegram alerts with photos">
  <img src="media/software/telegram/Screenshot%202026-10-05%20100525.png" width="30%" alt="Alerts with visitor photos">
</p>
<p align="center">
  <img src="media/software/telegram/photo_2026-10-05_10-05-02.jpg" width="60%" alt="Full-resolution photo sent with an alert">
</p>
<p align="center"><sub>The 1600×1200 photo that comes with a ring alert</sub></p>

---

## 9. Assemble, mount and test

**9.1 Fit the electronics.** The RGB LED goes behind the light ring in the base. The XIAO, with
its camera behind the front hole, and the button mount on the cover.

<p align="center">
  <img src="media/assembly/IMG_20261004_091343.jpg" width="32%" alt="RGB LED fitted in the base">
  <img src="media/assembly/IMG_20261004_091409.jpg" width="32%" alt="XIAO and button fitted in the cover">
  <img src="media/test/IMG_20261004_184410.jpg" width="32%" alt="Everything wired inside the enclosure">
</p>

**9.2 Close it up** and check the ring lights through the cover.

<p align="center">
  <img src="media/assembly/IMG_20261004_091422.jpg" width="32%" alt="Closed doorbell">
  <img src="media/test/IMG_20261004_204210.jpg" width="32%" alt="Light ring on during the day">
  <img src="media/test/IMG_20261004_204219.jpg" width="32%" alt="Light ring at night">
</p>

**9.3 Mount it.** Screw the wall plate at the door, then slide the doorbell onto its hook.

<p align="center">
  <img src="media/product/mount-wall3.jpg" width="24%" alt="Wall mount plate">
  <img src="media/product/Dorbel7.jpg" width="24%" alt="Doorbell back and the wall mount">
  <img src="media/product/DorbelSide1.jpg" width="24%" alt="Sliding the doorbell onto the mount">
  <img src="media/product/photo_2026-10-05_17-58-13.jpg" width="24%" alt="Doorbell beside the mount">
</p>

**9.4 End-to-end test**

- [ ] Console shows `Camera reachable at <ip>` and `Telegram bot initialized successfully`
- [ ] LED is green, and the dashboard shows SYSTEM `● ONLINE` with live video
- [ ] AI panel shows `● RUNNING`. Walk up, and a person badge, boxed photo and Telegram `👤` alert follow
- [ ] Press the button: chime at the door, LED red, DOORBELL `PRESSED`, Telegram `🔔` photo alert
- [ ] **Test door speaker** gives two beeps. **Hold to talk** carries your voice to the door
- [ ] `/photo` and `/status` answer in Telegram

<p align="center">
  <img src="media/product/Dorbel1.png" width="24%" alt="Dorbel finished">
  <img src="media/product/Dorbel5.jpg" width="24%" alt="Dorbel with the ring lit">
  <img src="media/product/DorbelSide.jpg" width="24%" alt="Dorbel side profile">
  <img src="media/product/DorbelBack.jpg" width="24%" alt="Dorbel back">
</p>

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Video never loads, alerts have no photo, log says `NOT reachable` | XIAO and UNO Q are on different networks. Put both on the same 2.4 GHz Wi-Fi. |
| LED blue / SYSTEM `XIAO OFFLINE` | I²C wiring: A4/A5, common GND, 4.7 kΩ pull-ups. XIAO must be in `APP_MODE_DORBEL`. |
| App Lab: `Variable "TELEGRAM_BOT_TOKEN" is required` | Keep the placeholder under `arduino:telegram_bot` in `app.yaml`. |
| `Telegram disabled` in the console | Token missing from `python/dorbel_config.py`. |
| No alerts, event `No enabled Telegram users` | Enable the user on the `/users` page. |
| AI says `WAITING FOR CAMERA` | Same cause as "video never loads". |
| AI misses people, or fires on shadows | Lower or raise `PERSON_CONFIDENCE`, and check the camera framing. |
| TALK says the microphone needs the secure dashboard | Use `https://…:8443`, not `http://…:8000`. |

More symptoms, with causes, are in [`docs/REFERENCE.md`](docs/REFERENCE.md#troubleshooting).

---

## How AI was used in this project

**1. AI on the device.** Person detection is a neural network running on the UNO Q, with no
cloud service:

| | |
|---|---|
| Brick | App Lab **Video Object Detection** (`arduino:video_object_detection`) |
| Model | **YoloX nano**, a COCO-trained object detector packaged by Edge Impulse (`yolo-x-nano.eim`) |
| Runs in | the brick's Edge Impulse model-runner container on the UNO Q's Linux side |
| Input | XIAO camera frames, 320×240, through `XiaoCamera` |
| Output used | `person` boxes and confidence → dashboard badge, boxed snapshot, VISITOR state, Telegram alert |

Why this model: it is App Lab's default general-purpose detector for the UNO Q, it ships ready
to run, and it returns **bounding boxes** that can be counted and drawn. The other option,
the *Person classification* model for the Video Image Classification brick, only answers
"person / no person" for the whole frame. Because the brick supports Edge Impulse models, a
custom model (for example "parcel at the door") can replace YoloX later without changing the
code.

**2. AI while building it.** An AI coding agent (Claude Code) worked alongside the maker:

- writing and debugging the XIAO firmware and the App Lab Python app (Bridge polling, the
  HTTPS dashboard, the intercom relay),
- reading the App Lab brick sources on the UNO Q to fit them to this hardware. That produced
  the `XiaoCamera` adapter, the shared `CameraHub`, and the `remote_camera_0` and
  token-placeholder settings in `app.yaml`,
- moving the alerts onto the Telegram Bot brick, and testing on the board from the container
  logs,
- writing this documentation.

The maker designed the hardware and enclosure, chose the features, and tested every step on the
real board.

---

## Project files

```
app.yaml                 App Lab app: ports 8000/8443, Video Object Detection + Telegram Bot bricks
sketch/sketch.ino        MCU: button, RGB LED, I²C master (Wire2 on A4/A5), Bridge functions
python/main.py           Linux: Bridge polling, state, wires the bricks together
python/camera_hub.py     shared XIAO stream + XiaoCamera for the AI brick
python/vision.py         person detection (Video Object Detection brick)
python/notifier.py       Telegram Bot brick: commands, user approval, alerts
python/dashboard.*       HTTPS dashboard: video, AI panel, push-to-talk, events
python/users.html        approve Telegram users
python/dorbel_config_example.py   settings template → dorbel_config.py
xiao/                    PlatformIO firmware: camera, mic, speaker, intercom, I²C slave
unoq_tests/              earlier step apps (I²C, IO, ring)
stl/                     enclosure and wall mount
media/                   all photos in this guide
docs/REFERENCE.md        pin map, protocol, subsystem tests, demo script, full troubleshooting
```

**Credits:** the dashboard and Telegram flow follow
[Q04 Trillo](https://gitlab.com/supermoderno/q04_trillo). The speaker wiring follows
[Xiaozhi-for-XiaoESP32S3](https://github.com/TechTalkies/Xiaozhi-for-XiaoESP32S3).
