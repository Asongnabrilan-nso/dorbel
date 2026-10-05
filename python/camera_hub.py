# One connection to the XIAO camera, shared by everything on the UNO Q.
#
#   XIAO :81/stream ──► CameraHub ──┬──► dashboard /stream (any number of viewers)
#                                   └──► XiaoCamera ──► Video Object Detection brick
#
# The XIAO's stream server serves a single client at a time, so the dashboard
# and the AI brick can't each open their own stream. The hub reads it once and
# hands the latest JPEG to whoever asks. For a full-resolution photo it pauses
# the stream briefly, because /capture competes with it for the sensor.

import threading
import time
import urllib.request

import cv2
import numpy as np

from arduino.app_peripherals.camera import BaseCamera

SOI = b"\xff\xd8"  # JPEG start / end markers
EOI = b"\xff\xd9"


class CameraHub:
    def __init__(self, get_ip):
        self._get_ip = get_ip
        self._cond = threading.Condition()
        self._jpeg = None
        self._seq = 0
        self._paused = threading.Event()
        self._capture_lock = threading.Lock()
        self.connected = False
        threading.Thread(target=self._run, daemon=True, name="CameraHub").start()

    def latest(self):
        """(seq, jpeg) of the newest frame; jpeg is None before the first one."""
        with self._cond:
            return self._seq, self._jpeg

    def wait_newer(self, seq, timeout=2.0):
        """Blocks until a frame newer than seq arrives. Returns (seq, jpeg) or (seq, None)."""
        with self._cond:
            self._cond.wait_for(lambda: self._seq != seq, timeout)
            if self._seq == seq:
                return seq, None
            return self._seq, self._jpeg

    def capture(self, timeout=15):
        """Full-resolution JPEG from the XIAO's /capture, or None."""
        ip = self._get_ip()
        if not ip:
            return None
        with self._capture_lock:
            self._paused.set()
            try:
                for _ in range(20):  # let the stream reader hang up first
                    if not self.connected:
                        break
                    time.sleep(0.1)
                for attempt in range(2):
                    try:
                        with urllib.request.urlopen(f"http://{ip}/capture", timeout=timeout) as resp:
                            return resp.read()
                    except Exception as e:
                        print(f"Capture attempt {attempt + 1} from {ip} failed: {e}")
                return None
            finally:
                self._paused.clear()

    def _publish(self, jpeg):
        with self._cond:
            self._jpeg = jpeg
            self._seq += 1
            self._cond.notify_all()

    def _run(self):
        while True:
            ip = self._get_ip()
            if not ip or self._paused.is_set():
                time.sleep(0.2 if ip else 1)
                continue
            try:
                with urllib.request.urlopen(f"http://{ip}:81/stream", timeout=5) as resp:
                    self.connected = True
                    self._read_mjpeg(resp, ip)
            except Exception as e:
                print(f"Camera hub: stream from {ip} lost: {e}")
            self.connected = False
            if not self._paused.is_set():
                time.sleep(2)

    def _read_mjpeg(self, resp, ip):
        # Cut frames on the JPEG markers instead of trusting the multipart
        # headers; it survives partial reads and stray boundaries.
        buf = b""
        while self._get_ip() == ip and not self._paused.is_set():
            chunk = resp.read1(65536)
            if not chunk:
                return
            buf += chunk
            while True:
                start = buf.find(SOI)
                end = buf.find(EOI, start + 2) if start >= 0 else -1
                if end < 0:
                    break
                self._publish(buf[start:end + 2])
                buf = buf[end + 2:]
            if len(buf) > 2_000_000:  # garbage, start over
                buf = b""


class XiaoCamera(BaseCamera):
    """App Lab camera backed by the hub, so the AI brick sees the XIAO like a local camera."""

    def __init__(self, hub, resolution=(320, 240), fps=5):
        super().__init__(resolution=resolution, fps=fps)
        self.name = "XiaoCamera"
        self._hub = hub
        self._seq = 0

    def _open_camera(self):
        self._set_status("connected")

    def _close_camera(self):
        self._set_status("disconnected")

    def _read_frame(self):
        self._seq, jpeg = self._hub.wait_newer(self._seq, timeout=1.0)
        if jpeg is None:
            return None
        frame = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
        if frame is None:
            return None
        # The model runner expects one frame size; /capture briefly switches the
        # XIAO to a larger one.
        w, h = self.resolution
        if frame.shape[1] != w or frame.shape[0] != h:
            frame = cv2.resize(frame, (w, h))
        return frame
