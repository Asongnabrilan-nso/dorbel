# Dorbel step 14 - Linux-side test of the MCU Bridge API.
#
# Polls the STM32 every 0.5 s and prints the doorbell latch and XIAO status.
# When a press is seen it is reported once and cleared with clear_event(),
# so every press shows up as a separate RING event.

import time

from arduino.app_utils import App, Bridge


def loop():
    try:
        doorbell = Bridge.call("get_doorbell_state")
        xiao = Bridge.call("get_xiao_status")
    except Exception as e:
        # The MCU registers its functions a moment after boot; retry quietly.
        print(f"bridge not ready: {e}")
        time.sleep(1)
        return

    xiao_text = "offline" if xiao < 0 else f"0x{xiao:02X}"
    print(f"doorbell={doorbell} xiao={xiao_text}")

    if doorbell == 1:
        print("RING event received from MCU")
        Bridge.call("clear_event")

    time.sleep(0.5)


App.run(user_loop=loop)
