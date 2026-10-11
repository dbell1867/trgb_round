#!/usr/bin/env python3
"""Read an ESP32 board's serial output from a non-interactive context.

Why this exists: `pio device monitor` needs a real TTY and fails in a
tool/script with `termios: Inappropriate ioctl for device`. This uses pyserial
(installed alongside PlatformIO) to read the port directly.

Run with PlatformIO's bundled Python so pyserial is available:
    ~/.platformio/penv/bin/python read-serial.py [PORT] [SECONDS]

Defaults: PORT=/dev/ttyACM0, SECONDS=6
"""
import sys
import time

import serial  # from pyserial, bundled with PlatformIO

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0

ser = serial.Serial(port, 115200, timeout=1)
# Pulse DTR to reset the board so we capture the boot header + fresh output.
ser.dtr = False
time.sleep(0.1)
ser.dtr = True

end = time.time() + secs
print(f"---- serial output ({port}, {secs:g}s) ----")
while time.time() < end:
    line = ser.readline().decode(errors="replace").rstrip()
    if line:
        print(line)
ser.close()
print("---- end ----")
