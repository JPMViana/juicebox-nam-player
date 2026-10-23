#!/usr/bin/env python3
"""JBB: reboot the Juice-Box (a clean state after a test left core 1 stuck)."""
import time
import mido
n = [a for a in mido.get_output_names() if "armaria" in a or "Juice" in a][0]
out = mido.open_output(n)
time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJBB")); time.sleep(0.3); out.close()
print("reboot enviado")
