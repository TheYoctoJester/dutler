#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: Apache-2.0

# Stream DUTler's debug-log port (CDC2) to stdout.
# Usage: python3 tools/debug_capture.py [PORT]
# With no PORT it auto-picks the highest-numbered CDC port (the debug log):
#   macOS  /dev/cu.usbmodem*5   Linux  /dev/ttyACM2
# (assumes only DUTler is attached; pass PORT explicitly otherwise).
import glob
import os
import select
import sys
import termios


def pick_debug_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*")) or sorted(glob.glob("/dev/ttyACM*"))
    return ports[-1] if ports else None


port = sys.argv[1] if len(sys.argv) > 1 else pick_debug_port()
if not port:
    sys.exit("no DUTler serial port found; pass one explicitly")

fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = a[1] = a[3] = 0
a[2] = termios.CREAD | termios.CLOCAL | termios.CS8
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)

print(f"# tailing {port} (Ctrl-C to stop)", file=sys.stderr)
try:
    while True:
        r, _, _ = select.select([fd], [], [], 0.5)
        if r:
            try:
                data = os.read(fd, 256)
            except BlockingIOError:
                continue
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.flush()
except KeyboardInterrupt:
    pass
finally:
    os.close(fd)
