# Telegram alerts for Dorbel, built on the App Lab "Telegram Bot" brick.
#
#   1. a homeowner opens the bot and sends /start -> registered, alerts off
#   2. they are enabled on the dashboard's /users page
#   3. a doorbell press sends enabled users a photo + alert text
#   4. the AI person detector sends enabled users a photo when someone arrives
#
# Enabled users can also ask the bot for /photo and /status at any time.
# The token comes from TELEGRAM_BOT_TOKEN in python/dorbel_config.py (kept out
# of git), or from the Telegram Bot brick configuration in App Lab.

import os
import sqlite3
import threading
import time

from arduino.app_bricks.telegram_bot import TelegramBot, Sender, Message

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dorbel_users.db")
ALERT_INTERVAL_S = 10  # at most one ring alert per 10 s, like Trillo
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
    def __init__(self, token, log_event, status_text, snapshot):
        """status_text() -> str for /status; snapshot() -> JPEG bytes or None for /photo."""
        self.log_event = log_event
        self.status_text = status_text
        self.snapshot = snapshot
        self.users = UserDB()
        self._last_alert = {}
        self.bot = None

        # A real token always has a ":"; app.yaml only holds a placeholder by default.
        env_token = os.getenv("TELEGRAM_BOT_TOKEN", "")
        token = token or (env_token if ":" in env_token else "")
        if not token:
            print("Telegram disabled: set TELEGRAM_BOT_TOKEN in the Telegram Bot brick "
                  "configuration or in python/dorbel_config.py")
            return
        # Created before App.run(), so App Lab starts and stops the brick with the app.
        self.bot = TelegramBot(token=token)
        self.bot.add_command("start", self._cmd_start, "Register for door alerts")
        self.bot.add_command("photo", self._cmd_photo, "Photo from the door camera")
        self.bot.add_command("status", self._cmd_status, "Dorbel system status")
        self.bot.add_command("help", self._cmd_help, "What this bot can do")

    @property
    def enabled(self):
        return self.bot is not None

    # ---- Commands --------------------------------------------------------

    def _cmd_start(self, sender: Sender, message: Message):
        name = " ".join(filter(None, [sender.first_name, sender.last_name]))
        name = name or sender.username or f"user_{sender.chat_id}"
        if self.users.register(sender.chat_id, name):
            self.log_event(f"Telegram user registered: {name}")
        sender.reply("Welcome to Dorbel 🔔\n\nYou are registered. Ask the homeowner to "
                     "enable you on the dashboard's Users page to receive door alerts.")

    def _allowed(self, sender):
        if sender.chat_id in self.users.enabled_chat_ids():
            return True
        sender.reply("You are not enabled yet. Send /start, then ask the homeowner to "
                     "enable you on the dashboard's Users page.")
        return False

    def _cmd_photo(self, sender: Sender, message: Message):
        if not self._allowed(sender):
            return
        photo = self.snapshot()
        if photo:
            sender.reply_photo(photo, f"📷 Door camera, {time.strftime('%H:%M:%S')}")
        else:
            sender.reply("📷 Camera not reachable right now.")

    def _cmd_status(self, sender: Sender, message: Message):
        if self._allowed(sender):
            sender.reply(self.status_text())

    def _cmd_help(self, sender: Sender, message: Message):
        sender.reply("🔔 Dorbel bot\n\n"
                     "/start - register for door alerts\n"
                     "/photo - photo from the door camera\n"
                     "/status - doorbell, camera and AI status\n\n"
                     "Alerts arrive when someone rings, and when the AI sees a person at the door.")

    # ---- Alerts ----------------------------------------------------------

    def alert(self, text, photo=None, kind="ring", interval=ALERT_INTERVAL_S):
        """Sends text + photo to enabled users without blocking the caller.

        photo is JPEG bytes, a function returning them, or None. Each kind of
        alert is rate-limited on its own, so a person alert never hides a ring.
        """
        if not self.bot:
            return
        now = time.time()
        if now - self._last_alert.get(kind, 0) < interval:
            return
        self._last_alert[kind] = now
        threading.Thread(target=self._send_alert, args=(text, photo), daemon=True).start()

    def _send_alert(self, text, photo):
        chat_ids = self.users.enabled_chat_ids()
        if not chat_ids:
            self.log_event("No enabled Telegram users to alert")
            return

        if callable(photo):
            photo = photo()
        if not photo:
            self.log_event("Snapshot failed: camera not reachable")
            text += "\n\n📷 No photo: camera not reachable."

        # The brick retries each send itself.
        sent = 0
        for chat_id in chat_ids:
            if photo:
                ok = self.bot.send_photo(chat_id, photo, text)
            else:
                ok = self.bot.send_message(chat_id, text)
            sent += bool(ok)
        self.log_event(f"Telegram alert sent to {sent}/{len(chat_ids)}")
