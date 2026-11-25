#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Send a VESC terminal command and print the replies.

    vesc_term.py [command] [seconds] [device]

Also counts COMM_CUSTOM_APP_DATA packets, which is how you tell whether the
arbiter is running and streaming feedback.
"""
import sys, time
from vesc_packet import pkt, frames, open_port

cmd = sys.argv[1] if len(sys.argv) > 1 else 'arb'
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
dev = sys.argv[3] if len(sys.argv) > 3 else '/dev/ttyACM0'

s = open_port(dev)
s.write(pkt(bytes([20]) + cmd.encode()))      # COMM_TERMINAL_CMD

buf, appdata, t0 = bytearray(), 0, time.time()
while time.time() - t0 < dur:
    buf += s.read(1024)
    for pl in frames(buf):
        if pl[0] == 21:                        # COMM_PRINT
            sys.stdout.write(pl[1:].decode(errors='replace'))
        elif pl[0] == 36:                      # COMM_CUSTOM_APP_DATA
            appdata += 1
print()
print(f"[custom app data {appdata}개 / {dur}초]")
