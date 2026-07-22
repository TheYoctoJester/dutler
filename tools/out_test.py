# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: Apache-2.0

import os
import sys
import select
import time
import termios

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem1103"
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
# raw mode
attr = termios.tcgetattr(fd)
attr[0] = attr[1] = attr[3] = 0          # iflag, oflag, lflag
attr[2] = termios.CREAD | termios.CLOCAL | termios.CS8
termios.tcsetattr(fd, termios.TCSANOW, attr)

def drain(timeout=0.4):
    out = b""
    end = time.time() + timeout
    while time.time() < end:
        r,_,_ = select.select([fd], [], [], 0.1)
        if r:
            try:
                out += os.read(fd, 256)
            except BlockingIOError:
                pass
    return out.decode(errors="replace")

def read_reply(timeout=2.0):
    """Read until the framed terminator line: `OK` or `ERR <msg>`. Returns the
    full reply text. No timeout guessing — the safety timeout only guards a dead
    link. (The banner has no terminator, so drain() it separately at startup.)"""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r,_,_ = select.select([fd], [], [], 0.1)
        if r:
            try:
                buf += os.read(fd, 256)
            except BlockingIOError:
                pass
            for line in buf.decode(errors="replace").splitlines():
                if line == "OK" or line.startswith("ERR"):
                    return buf.decode(errors="replace")
    return buf.decode(errors="replace")  # timeout fallback (link stalled)

def send(cmd):
    os.write(fd, (cmd + "\r\n").encode())
    resp = read_reply()
    print(f">>> {cmd}\n{resp}", end="")
    print("-"*40)

drain()                       # clear the connect banner (it has no terminator)
for c in ["help", "status", "out 1 on", "out 2 on", "status",
          "out 1 off", "out 3 toggle", "status", "out 9 on", "bogus"]:
    send(c)
    time.sleep(0.1)
os.close(fd)
