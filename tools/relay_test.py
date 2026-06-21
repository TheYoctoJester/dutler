# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: GPL-3.0-or-later

import os, sys, select, time, termios

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
            try: out += os.read(fd, 256)
            except BlockingIOError: pass
    return out.decode(errors="replace")

def send(cmd):
    os.write(fd, (cmd + "\r\n").encode())
    resp = drain()
    print(f">>> {cmd}\n{resp}", end="")
    print("-"*40)

drain()                       # clear any banner
for c in ["help", "status", "relay 1 on", "relay 2 on", "status",
          "relay 1 off", "relay 3 pulse 300", "status", "relay 9 on", "bogus"]:
    send(c)
    time.sleep(0.1)
os.close(fd)
