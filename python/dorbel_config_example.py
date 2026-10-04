# Dorbel settings. Copy this file to dorbel_config.py (gitignored) and edit it.
# Without a dorbel_config.py the app runs with these defaults (Telegram off).

# Bot token from @BotFather. Empty = Telegram alerts disabled.
TELEGRAM_BOT_TOKEN = ""

# Port the dashboard is served on (also listed under ports: in app.yaml).
DASHBOARD_PORT = 8000

# HTTPS port for the dashboard (self-signed certificate). The browser only
# allows the Hold-to-talk microphone on https://. Also listed in app.yaml.
DASHBOARD_HTTPS_PORT = 8443

# Dashboard link put in Telegram alerts. Empty = https://<this board's IP>:<https port>/.
DASHBOARD_URL = ""

# The XIAO reports its own IP over I2C. Set this only to override it.
XIAO_HOST = ""
