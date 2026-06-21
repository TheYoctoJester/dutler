# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: Apache-2.0

import os, sys, select, time, termios, threading

def raw(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd); a[0]=a[1]=a[3]=0
    a[2]=termios.CREAD|termios.CLOCAL|termios.CS8
    a[4]=a[5]=termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

dbg = raw("/dev/cu.usbmodem1105")   # debug log (open it -> asserts DTR -> firmware starts logging)
rel = raw("/dev/cu.usbmodem1103")   # relay control
br  = raw("/dev/cu.usbmodem1101")   # bridge (baud change triggers a log)
time.sleep(0.3)

captured = bytearray()
stop = False
def reader():
    while not stop:
        r,_,_ = select.select([dbg],[],[],0.1)
        if r:
            try: captured.extend(os.read(dbg,256))
            except BlockingIOError: pass
t = threading.Thread(target=reader); t.start()
time.sleep(0.2)

# drive activity on the other two ports
for c in ["relay 1 on", "relay 2 toggle", "relay 1 off", "relay 3 pulse 250"]:
    os.write(rel, (c+"\r\n").encode()); time.sleep(0.15)

# change bridge baud -> line_coding_cb logs it
a = termios.tcgetattr(br); a[4]=a[5]=termios.B9600
termios.tcsetattr(br, termios.TCSANOW, a)
time.sleep(0.4)

stop = True; t.join()
for fd in (dbg, rel, br): os.close(fd)
print("=== captured on debug port (1105) ===")
sys.stdout.write(captured.decode(errors="replace"))
