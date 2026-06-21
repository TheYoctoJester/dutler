#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

# Loopback test for the UART bridge port (CDC0).
# REQUIRES a jumper wire between GP0 (pin 1) and GP1 (pin 2).
# Usage: python3 tools/bridge_test.py [/dev/cu.usbmodemXXXX]
import os, sys, select, time, termios

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem1101"
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
attr = termios.tcgetattr(fd)
attr[0] = attr[1] = attr[3] = 0
attr[2] = termios.CREAD | termios.CLOCAL | termios.CS8
# Set a baud so tud_cdc_line_coding_cb reconfigures the hardware UART.
for k in ("B115200",):
    b = getattr(termios, k)
attr[4] = attr[5] = b
termios.tcsetattr(fd, termios.TCSANOW, attr)
time.sleep(0.2)

def drain(timeout=0.5):
    out = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: out += os.read(fd, 256)
            except BlockingIOError: pass
    return out

drain()
msg = b"Hello, bridge!"
os.write(fd, msg)
echo = drain()
os.close(fd)

print(f"sent:     {msg!r}")
print(f"received: {echo!r}")
print("PASS — loopback OK" if echo == msg else
      "FAIL — check the GP0<->GP1 jumper (and that you opened the *bridge* port)")
