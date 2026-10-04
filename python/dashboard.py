# Dorbel dashboard HTTP server (standard library only).
#
#   GET  /                        dashboard page (video + status + TALK + events)
#   GET  /users                   Telegram user management page
#   GET  /api/state               JSON status, polled by the dashboard every second
#   GET  /api/users               registered Telegram users
#   POST /api/users/<id>/toggle   enable / disable alerts for a user
#   POST /api/users/<id>/delete   remove a user

import json
import os
import re
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PAGES = {"/": "dashboard.html", "/users": "users.html"}


def local_ip():
    """The address other devices on the LAN use to reach this board."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("8.8.8.8", 80))  # no packet is sent for UDP connect
            return s.getsockname()[0]
        except OSError:
            return "127.0.0.1"


def start(state, notifier, port):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass  # the dashboard polls every second; keep the console readable

        def _send(self, code, body, content_type):
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, data, code=200):
            self._send(code, json.dumps(data).encode(), "application/json")

        def do_GET(self):
            path = self.path.split("?")[0]
            if path in PAGES:
                with open(os.path.join(HERE, PAGES[path]), "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            elif path == "/api/state":
                data = state.snapshot()
                data["telegram"] = notifier.enabled
                self._json(data)
            elif path == "/api/users":
                self._json({"users": notifier.users.all(), "telegram": notifier.enabled})
            else:
                self._json({"error": "not found"}, 404)

        def do_POST(self):
            m = re.fullmatch(r"/api/users/(-?\d+)/(toggle|delete)", self.path)
            if not m:
                self._json({"error": "not found"}, 404)
                return
            chat_id, action = int(m.group(1)), m.group(2)
            ok = notifier.users.toggle(chat_id) if action == "toggle" else notifier.users.remove(chat_id)
            self._json({"ok": ok}, 200 if ok else 404)

    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server
