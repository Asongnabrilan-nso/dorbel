# Person detection on the XIAO's live video, using the App Lab
# "Video Object Detection" brick (YoloX nano, COCO classes, runs on the UNO Q).
#
#   CameraHub -> XiaoCamera -> brick's model runner -> on_detect_all() -> here
#
# Only the "person" class is used. The dashboard shows the latest annotated
# frame (/detection.jpg) and the count/confidence from snapshot().

import io
import threading
import time

from PIL import Image

from arduino.app_bricks.video_objectdetection import VideoObjectDetection
from arduino.app_utils.image import draw_bounding_boxes

from camera_hub import XiaoCamera

PERSON = "person"
PRESENT_HOLD_S = 3  # still "present" this long after the last detection


class PersonDetector:
    def __init__(self, hub, on_arrival, confidence=0.5):
        """on_arrival(jpeg) is called once each time a person appears after an empty scene."""
        self._hub = hub
        self._on_arrival = on_arrival
        self._lock = threading.Lock()
        self._count = 0
        self._confidence = 0.0
        self._last_seen = 0.0
        self._jpeg = None
        self._seq = 0

        self.detector = VideoObjectDetection(
            camera=XiaoCamera(hub),
            confidence=confidence,
            debounce_sec=0.0,
            camera_preview=True,  # gives us the exact frame the model saw
        )

        # The brick only accepts plain functions, not bound methods.
        def on_detect_all(detections: dict, frame: bytes = None):
            self._on_detections(detections, frame)

        self.detector.on_detect_all(on_detect_all)

    def _on_detections(self, detections: dict, frame: bytes = None):
        people = detections.get(PERSON, [])
        if not people:
            return

        image = frame or self._hub.latest()[1]
        jpeg = None
        if image:
            try:
                # The helper labels and colours boxes by confidence in percent.
                pct = [{**p, "confidence": p["confidence"] * 100} for p in people]
                boxed = draw_bounding_boxes(image, {PERSON: pct}).convert("RGB")
                out = io.BytesIO()
                boxed.save(out, "JPEG", quality=85)
                jpeg = out.getvalue()
            except Exception as e:
                print(f"Could not draw detection boxes: {e}")
                jpeg = image

        with self._lock:
            arrived = not self._present_now()
            self._count = len(people)
            self._confidence = max(p["confidence"] for p in people)
            self._last_seen = time.time()
            if jpeg:
                self._jpeg = jpeg
                self._seq += 1

        if arrived:
            self._on_arrival(jpeg)

    def _present_now(self):
        return time.time() - self._last_seen < PRESENT_HOLD_S

    def jpeg(self):
        with self._lock:
            return self._jpeg

    def snapshot(self):
        with self._lock:
            present = self._present_now()
            return {
                "running": self._hub.connected,
                "person": present,
                "count": self._count if present else 0,
                "confidence": round(self._confidence, 2) if present else 0,
                "last_seen": time.strftime("%H:%M:%S", time.localtime(self._last_seen))
                if self._last_seen else None,
                "frame": self._seq,
                "model": "YoloX nano (COCO)",
            }
