#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

# Find the running adapter's relay-control port and tell it to reboot into the
# USB bootloader (BOOTSEL), so the firmware can be reflashed without the button.
# Exit 0 if the reset command was sent, 1 if no relay port was found.
import glob, os, select, sys, termios, time

def raw(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd); a[0] = a[1] = a[3] = 0
    a[2] = termios.CREAD | termios.CLOCAL | termios.CS8
    a[4] = a[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

def drain(fd, t=0.4):
    out, end = b"", time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: out += os.read(fd, 256)
            except BlockingIOError: pass
    return out

# macOS: /dev/cu.usbmodem*   Linux: /dev/ttyACM*
for port in sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*")):
    try:
        fd = raw(port)
    except OSError:
        continue
    try:
        drain(fd, 0.1)
        os.write(fd, b"status\r\n")          # only the relay port answers with "relay ..."
        if b"relay" in drain(fd, 0.4):
            os.write(fd, b"bootsel\r\n")
            time.sleep(0.2)
            print(f"sent 'bootsel' to {port}")
            sys.exit(0)
    finally:
        try: os.close(fd)
        except OSError: pass

print("relay control port not found (already in BOOTSEL?)", file=sys.stderr)
sys.exit(1)
