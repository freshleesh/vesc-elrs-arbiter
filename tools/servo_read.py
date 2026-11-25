#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import sys, time, struct, statistics
from vesc_packet import frames, open_port
MODES = {0:"STOP/ESTOP",1:"MANUAL",2:"AUTO"}
s = open_port('/dev/ttyACM0')
buf, rows, t0 = bytearray(), [], time.time()
while time.time() - t0 < 3.0:
    buf += s.read(1024)
    for pl in frames(buf):
        if pl[0] == 36 and len(pl) >= 9 and pl[1] == 0xA5:
            mode = pl[2]
            servo, st, th = struct.unpack('>hhh', pl[3:9])
            rows.append((mode, servo/1000.0, st/1000.0, th/1000.0))
if not rows:
    print("피드백 없음"); sys.exit(1)
print(f"샘플 {len(rows)}개  mode={rows[-1][0]} ({MODES.get(rows[-1][0],'?')})")
for i, n in ((1,'servo'), (2,'rc_steer'), (3,'rc_throttle')):
    v = [r[i] for r in rows]
    print(f"{n:<12} min {min(v):+.4f}  median {statistics.median(v):+.4f}  max {max(v):+.4f}")
