# MCU-only test: all logic is in sketch/sketch.ino; output is in the
# App Lab console (Monitor). Linux side just keeps the app running.
from arduino.app_utils import App

App.run()
