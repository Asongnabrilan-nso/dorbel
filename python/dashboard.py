# Dorbel dashboard HTTP server (standard library only).
#
#   GET  /                        dashboard page (video + status + TALK + events)
#   GET  /users                   Telegram user management page
#   GET  /api/state               JSON status, polled by the dashboard every second
#   GET  /api/users               registered Telegram users
#   POST /api/users/<id>/toggle   enable / disable alerts for a user
#   POST /api/users/<id>/delete   remove a user
#   GET  /stream                  XIAO camera stream, relayed (XIAO :81/stream)
#   GET  /snapshot                one JPEG from the XIAO (XIAO /capture)
#   GET  /audio                   intercom WebSocket, relayed (XIAO ws /audio)
#
# Relaying the XIAO through here means a browser only has to reach the UNO Q,
# and the page stays on one origin, so it also works over HTTPS (needed for
# the microphone). HTTPS uses a self-signed certificate made on first start.

import json
import os
import re
import select
import socket
import ssl
import subprocess
import threading
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PAGES = {"/": "dashboard.html", "/users": "users.html"}
CERT_DIR = os.path.join(HERE, "certs")
CERT_FILE = os.path.join(CERT_DIR, "dorbel.crt")
KEY_FILE = os.path.join(CERT_DIR, "dorbel.key")


def local_ip():
    """The address other devices on the LAN use to reach this board."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("8.8.8.8", 80))  # no packet is sent for UDP connect
            return s.getsockname()[0]
        except OSError:
            return "127.0.0.1"


def _ensure_cert():
    """Self-signed certificate for the HTTPS dashboard. Returns False if none."""
    if os.path.exists(CERT_FILE) and os.path.exists(KEY_FILE):
        return True
    os.makedirs(CERT_DIR, exist_ok=True)
    try:
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "3650",
             "-subj", "/CN=dorbel", "-keyout", KEY_FILE, "-out", CERT_FILE],
            check=True, capture_output=True, timeout=60,
        )
        return True
    except Exception as e:
        print(f"HTTPS disabled, could not create a certificate: {e}")
        return False


def _pipe(a, b):
    """Copies bytes both ways until either side closes (a may be an SSL socket)."""
    socks = [a, b]
    try:
        while True:
            pending = getattr(a, "pending", lambda: 0)()
            ready = [a] if pending else select.select(socks, [], [], 120)[0]
            if not ready:
                return  # idle for 2 minutes
            for src in ready:
                data = src.recv(65536)
                if not data:
                    return
                (b if src is a else a).sendall(data)
    except OSError:
        pass


def start(state, notifier, port, https_port=None):
    def xiao_ip():
        with state.lock:
            return state.xiao_ip

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass  # the dashboard polls every second; keep the console readable

        def handle_one_request(self):
            try:
                super().handle_one_request()
            except (ssl.SSLError, ConnectionError, TimeoutError):
                self.close_connection = True  # e.g. browser rejected the certificate

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
                data["https_port"] = https_port
                self._json(data)
            elif path == "/api/users":
                self._json({"users": notifier.users.all(), "telegram": notifier.enabled})
            elif path == "/stream":
                self._relay_stream()
            elif path == "/snapshot":
                self._relay_snapshot()
            elif path == "/audio":
                self._relay_websocket()
            else:
                self._json({"error": "not found"}, 404)

        def _xiao_or_503(self):
            ip = xiao_ip()
            if not ip:
                self._json({"error": "XIAO address not known yet"}, 503)
            return ip

        def _relay_snapshot(self):
            ip = self._xiao_or_503()
            if not ip:
                return
            try:
                with urllib.request.urlopen(f"http://{ip}/capture", timeout=5) as resp:
                    self._send(200, resp.read(), "image/jpeg")
            except Exception as e:
                self._json({"error": f"camera unreachable at {ip}: {e}"}, 502)

        def _relay_stream(self):
            ip = self._xiao_or_503()
            if not ip:
                return
            try:
                upstream = urllib.request.urlopen(f"http://{ip}:81/stream", timeout=5)
            except Exception as e:
                self._json({"error": f"camera unreachable at {ip}: {e}"}, 502)
                return
            with upstream:
                self.send_response(200)
                self.send_header("Content-Type", upstream.headers["Content-Type"])
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                try:
                    while True:
                        chunk = upstream.read1(65536)
                        if not chunk:
                            break
                        self.wfile.write(chunk)
                except (OSError, ValueError):
                    pass  # viewer left or camera dropped
            self.close_connection = True

        def _relay_websocket(self):
            ip = self._xiao_or_503()
            if not ip:
                return
            try:
                upstream = socket.create_connection((ip, 80), timeout=5)
            except OSError as e:
                self._json({"error": f"intercom unreachable at {ip}: {e}"}, 502)
                return
            # Forward the browser's upgrade request as-is (Host rewritten), then
            # the XIAO answers the handshake and both sides speak WebSocket.
            lines = [f"GET /audio HTTP/1.1", f"Host: {ip}"]
            lines += [f"{k}: {v}" for k, v in self.headers.items()
                      if k.lower() not in ("host", "origin")]
            upstream.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
            upstream.settimeout(None)
            self.connection.settimeout(None)
            with upstream:
                _pipe(self.connection, upstream)
            self.close_connection = True

        def do_POST(self):
            m = re.fullmatch(r"/api/users/(-?\d+)/(toggle|delete)", self.path)
            if not m:
                self._json({"error": "not found"}, 404)
                return
            chat_id, action = int(m.group(1)), m.group(2)
            ok = notifier.users.toggle(chat_id) if action == "toggle" else notifier.users.remove(chat_id)
            self._json({"ok": ok}, 200 if ok else 404)

    servers = [ThreadingHTTPServer(("0.0.0.0", port), Handler)]
    if https_port and _ensure_cert():
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(CERT_FILE, KEY_FILE)
        secure = ThreadingHTTPServer(("0.0.0.0", https_port), Handler)
        # Handshake in the request thread, so one stuck client can't block accept().
        secure.socket = ctx.wrap_socket(secure.socket, server_side=True,
                                        do_handshake_on_connect=False)
        servers.append(secure)
    for server in servers:
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
    return len(servers) > 1  # True when HTTPS is up
