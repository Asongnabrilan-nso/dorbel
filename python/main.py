# Dorbel - UNO Q Linux side.
#
# Polls the STM32 over the Bridge, keeps the doorbell state and event log,
# serves the dashboard (dashboard.py), runs AI person detection on the live
# video (vision.py) and sends Telegram alerts (notifier.py).
#
#   button -> STM32 -> Bridge -> here -> Telegram + dashboard events
#   XIAO video -> CameraHub -> Video Object Detection brick -> person events
#
# The dashboard relays the XIAO's video and intercom audio (see dashboard.py),
# so browsers only need to reach this board. The XIAO reports its own IP over
# I2C, and it must be on the same network as this board.

import os
import socket
import sys
import threading
import time
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from arduino.app_utils import App, Bridge

import dashboard
from camera_hub import CameraHub
from notifier import TelegramNotifier
from vision import PersonDetector

try:
    import dorbel_config as config
except ImportError:
    import dorbel_config_example as config

STATUS_ALIVE = 0x01
STATUS_CAMERA = 0x02
STATUS_AUDIO = 0x04
STATUS_TALK = 0x08

PRESSED_SHOW_S = 10    # dashboard shows DOORBELL: PRESSED this long after a ring
VISITOR_WINDOW_S = 120 # VISITOR: PRESENT this long after a ring, talk or person


class DorbelState:
    """Shared between the Bridge loop and the dashboard's HTTP threads."""

    def __init__(self):
        self.lock = threading.Lock()
        self.mcu_online = False
        self.xiao_status = -1
        self.xiao_ip = ""
        self.talking = False
        self.last_ring = 0.0
        self.last_activity = 0.0
        self.events = deque(maxlen=50)

    def add_event(self, text):
        with self.lock:
            self.events.appendleft({"time": time.strftime("%H:%M:%S"), "text": text})
        print(f"[event] {text}")

    def snapshot(self):
        now = time.time()
        with self.lock:
            xiao_online = self.xiao_status >= 0
            return {
                "mcu_online": self.mcu_online,
                "xiao_online": xiao_online,
                "xiao_wifi": xiao_online and bool(self.xiao_status & STATUS_ALIVE),
                "camera": xiao_online and bool(self.xiao_status & STATUS_CAMERA),
                "audio": xiao_online and bool(self.xiao_status & STATUS_AUDIO),
                "talking": self.talking,
                "xiao_ip": self.xiao_ip,
                "doorbell_pressed": now - self.last_ring < PRESSED_SHOW_S,
                "visitor_present": now - self.last_activity < VISITOR_WINDOW_S,
                "door": None,  # V1 has no door sensor
                "events": list(self.events),
            }


state = DorbelState()


def current_xiao_ip():
    with state.lock:
        return state.xiao_ip


def snapshot_jpeg():
    """Newest live frame, a fallback when the full-resolution photo fails."""
    return camera.latest()[1]


def ring_photo():
    """Full-resolution photo for ring alerts and /photo."""
    return camera.capture() or snapshot_jpeg()


def status_text():
    s = state.snapshot()
    ai = detector.snapshot() if detector else None
    on = lambda ok: "✅" if ok else "❌"
    lines = [
        "🔔 Dorbel status",
        "",
        f"{on(s['mcu_online'])} UNO Q microcontroller",
        f"{on(s['xiao_online'])} XIAO link",
        f"{on(s['camera'] and camera.connected)} Camera",
        f"{on(ai and ai['running'])} AI person detection",
        "",
        f"Person at door: {'YES (' + str(ai['count']) + ')' if ai and ai['person'] else 'no'}",
        f"Last person seen: {(ai and ai['last_seen']) or '-'}",
        f"Dashboard: {dashboard_url}",
    ]
    return "\n".join(lines)


camera = CameraHub(current_xiao_ip)
notifier = TelegramNotifier(config.TELEGRAM_BOT_TOKEN, state.add_event, status_text, ring_photo)


def on_person_arrived(jpeg):
    with state.lock:
        state.last_activity = time.time()
    state.add_event("AI: person detected at the door")
    if getattr(config, "PERSON_ALERTS", True):
        notifier.alert(
            f"👤 Person at the door\n\nSeen by Dorbel's AI at {time.strftime('%H:%M')}.\n\n"
            f"Live video & talk: {dashboard_url}",
            jpeg or snapshot_jpeg(), kind="person",
            interval=getattr(config, "PERSON_ALERT_INTERVAL_S", 120))


detector = None
if getattr(config, "PERSON_DETECTION", True):
    detector = PersonDetector(camera, on_person_arrived,
                              confidence=getattr(config, "PERSON_CONFIDENCE", 0.5))

https_port = getattr(config, "DASHBOARD_HTTPS_PORT", 8443)
https_up = dashboard.start(state, notifier, camera, detector, config.DASHBOARD_PORT, https_port)

if config.DASHBOARD_URL:
    dashboard_url = config.DASHBOARD_URL
elif https_up:
    dashboard_url = f"https://{dashboard.local_ip()}:{https_port}/"
else:
    dashboard_url = f"http://{dashboard.local_ip()}:{config.DASHBOARD_PORT}/"
print(f"Dorbel dashboard: {dashboard_url}")
state.add_event("System started")


def check_xiao_reachable(ip):
    """Logs a clear event when the XIAO reports an IP this board can't reach."""
    try:
        socket.create_connection((ip, 80), timeout=3).close()
        state.add_event(f"Camera reachable at {ip}")
    except OSError:
        state.add_event(f"Camera at {ip} is NOT reachable - put the XIAO on "
                        f"the same Wi-Fi network as this board")


def ip_from_packed(packed):
    if not packed:
        return ""
    packed &= 0xFFFFFFFF
    return ".".join(str((packed >> shift) & 0xFF) for shift in (24, 16, 8, 0))


def on_ring():
    now = time.time()
    with state.lock:
        state.last_ring = now
        state.last_activity = now
        xiao_ip = state.xiao_ip
    state.add_event("Doorbell pressed")

    lines = [
        "🔔 DORBEL ALERT",
        "",
        "Someone is at the door.",
        "",
        "Doorbell: PRESSED",
        f"Time: {time.strftime('%H:%M')}",
        "",
        f"Live video & talk: {dashboard_url}",
    ]
    notifier.alert("\n".join(lines), ring_photo)


def loop():
    try:
        doorbell = Bridge.call("get_doorbell_state")
        xiao = Bridge.call("get_xiao_status")
    except Exception as e:
        # The MCU registers its functions a moment after boot; retry quietly.
        if state.mcu_online:
            state.add_event("MCU not answering")
        state.mcu_online = False
        print(f"bridge not ready: {e}")
        time.sleep(1)
        return

    try:
        xiao_ip = config.XIAO_HOST or ip_from_packed(Bridge.call("get_xiao_ip"))
    except Exception:
        xiao_ip = config.XIAO_HOST  # older sketch without get_xiao_ip

    talking = xiao >= 0 and bool(xiao & STATUS_TALK)

    with state.lock:
        was_mcu = state.mcu_online
        was_online = state.xiao_status >= 0
        was_talking = state.talking
        old_ip = state.xiao_ip
        state.mcu_online = True
        state.xiao_status = xiao
        state.talking = talking
        if xiao_ip:
            state.xiao_ip = xiao_ip
        if talking:
            state.last_activity = time.time()

    if not was_mcu:
        state.add_event("MCU connected")
    if (xiao >= 0) != was_online:
        state.add_event("XIAO online" if xiao >= 0 else "XIAO offline")
    if xiao_ip and xiao_ip != old_ip:
        state.add_event(f"Camera at {xiao_ip}")
        threading.Thread(target=check_xiao_reachable, args=(xiao_ip,), daemon=True).start()
    if talking != was_talking:
        state.add_event("Intercom started" if talking else "Intercom ended")

    if doorbell == 1:
        Bridge.call("clear_event")
        on_ring()

    time.sleep(0.25)


App.run(user_loop=loop)
