# Telegram alerts for Dorbel, following the Q04 Trillo flow:
#   1. a homeowner finds the bot and sends /start -> registered, disabled
#   2. they are enabled on the dashboard's /users page
#   3. every doorbell press sends enabled users a photo + alert text
#
# Talks to the Bot API directly with urllib, so App Lab needs no extra
# Python packages.

import json
import os
import sqlite3
import threading
import time
import urllib.request
import uuid

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dorbel_users.db")
ALERT_INTERVAL_S = 10  # at most one alert per 10 s, like Trillo
MAX_USERS = 10


class UserDB:
    def __init__(self, path=DB_PATH):
        self.path = path
        with self._connect() as db:
            db.execute(
                "CREATE TABLE IF NOT EXISTS users ("
                " chat_id INTEGER PRIMARY KEY,"
                " name TEXT,"
                " enabled INTEGER DEFAULT 0,"
                " created_at TEXT DEFAULT CURRENT_TIMESTAMP)"
            )

    def _connect(self):
        return sqlite3.connect(self.path)

    def register(self, chat_id, name):
        """Returns True if this is a new user."""
        with self._connect() as db:
            if db.execute("SELECT 1 FROM users WHERE chat_id = ?", (chat_id,)).fetchone():
                db.execute("UPDATE users SET name = ? WHERE chat_id = ?", (name, chat_id))
                return False
            if db.execute("SELECT COUNT(*) FROM users").fetchone()[0] >= MAX_USERS:
                return False
            db.execute("INSERT INTO users (chat_id, name) VALUES (?, ?)", (chat_id, name))
            return True

    def all(self):
        with self._connect() as db:
            rows = db.execute(
                "SELECT chat_id, name, enabled, created_at FROM users ORDER BY created_at"
            ).fetchall()
        return [
            {"chat_id": r[0], "name": r[1], "enabled": bool(r[2]), "created_at": r[3]}
            for r in rows
        ]

    def enabled_chat_ids(self):
        with self._connect() as db:
            return [r[0] for r in db.execute("SELECT chat_id FROM users WHERE enabled = 1")]

    def toggle(self, chat_id):
        with self._connect() as db:
            cur = db.execute("UPDATE users SET enabled = 1 - enabled WHERE chat_id = ?", (chat_id,))
            return cur.rowcount > 0

    def remove(self, chat_id):
        with self._connect() as db:
            return db.execute("DELETE FROM users WHERE chat_id = ?", (chat_id,)).rowcount > 0


class TelegramNotifier:
    def __init__(self, token, log_event):
        self.token = token
        self.log_event = log_event
        self.users = UserDB()
        self._last_alert = 0.0
        if token:
            threading.Thread(target=self._poll_updates, daemon=True).start()
        else:
            print("Telegram disabled: set TELEGRAM_BOT_TOKEN in python/dorbel_config.py")

    @property
    def enabled(self):
        return bool(self.token)

    # ---- Bot API -------------------------------------------------------

    def _api(self, method, payload=None, timeout=10):
        url = f"https://api.telegram.org/bot{self.token}/{method}"
        data = json.dumps(payload or {}).encode()
        req = urllib.request.Request(url, data, {"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.load(resp)

    def _send_photo(self, chat_id, photo, caption):
        boundary = uuid.uuid4().hex
        parts = []
        for name, value in (("chat_id", str(chat_id)), ("caption", caption)):
            parts.append(
                f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode()
            )
        parts.append(
            f'--{boundary}\r\nContent-Disposition: form-data; name="photo"; filename="door.jpg"\r\n'
            "Content-Type: image/jpeg\r\n\r\n".encode()
            + photo
            + b"\r\n"
        )
        parts.append(f"--{boundary}--\r\n".encode())
        req = urllib.request.Request(
            f"https://api.telegram.org/bot{self.token}/sendPhoto",
            b"".join(parts),
            {"Content-Type": f"multipart/form-data; boundary={boundary}"},
        )
        with urllib.request.urlopen(req, timeout=15) as resp:
            return json.load(resp)

    # ---- /start registration -------------------------------------------

    def _poll_updates(self):
        offset = 0
        while True:
            try:
                result = self._api("getUpdates", {"offset": offset, "timeout": 25}, timeout=35)
                for update in result.get("result", []):
                    offset = update["update_id"] + 1
                    self._handle_update(update)
            except Exception as e:
                print(f"Telegram polling error: {e}")
                time.sleep(5)

    def _handle_update(self, update):
        message = update.get("message") or {}
        text = (message.get("text") or "").strip()
        chat = message.get("chat") or {}
        if not text.startswith("/start") or "id" not in chat:
            return

        sender = message.get("from") or {}
        name = " ".join(filter(None, [sender.get("first_name"), sender.get("last_name")]))
        name = name or sender.get("username") or f"user_{chat['id']}"

        if self.users.register(chat["id"], name):
            self.log_event(f"Telegram user registered: {name}")
        self._api("sendMessage", {
            "chat_id": chat["id"],
            "text": "Welcome to Dorbel 🔔\n\nYou are registered. Ask the homeowner to "
                    "enable you on the dashboard's Users page to receive door alerts.",
        })

    # ---- Alerts --------------------------------------------------------

    def alert(self, text, photo_url=None):
        """Sends text (with a camera photo when available) without blocking the caller."""
        if not self.token:
            return
        now = time.time()
        if now - self._last_alert < ALERT_INTERVAL_S:
            return
        self._last_alert = now
        threading.Thread(target=self._send_alert, args=(text, photo_url), daemon=True).start()

    def _send_alert(self, text, photo_url):
        chat_ids = self.users.enabled_chat_ids()
        if not chat_ids:
            self.log_event("No enabled Telegram users to alert")
            return

        photo = None
        if photo_url:
            try:
                with urllib.request.urlopen(photo_url, timeout=3) as resp:
                    photo = resp.read()
            except Exception as e:
                print(f"Snapshot failed ({photo_url}): {e}")

        sent = 0
        for chat_id in chat_ids:
            try:
                if photo:
                    self._send_photo(chat_id, photo, text)
                else:
                    self._api("sendMessage", {"chat_id": chat_id, "text": text})
                sent += 1
            except Exception as e:
                print(f"Telegram send to {chat_id} failed: {e}")
        self.log_event(f"Telegram alert sent to {sent}/{len(chat_ids)}")
