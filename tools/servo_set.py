#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Hold the steering servo at given positions. Needs SEL on AUTO (comm passthrough).

    servo_set.py 0.55                 hold 0.55 for 4 s
    servo_set.py 0.55 0.60 0.65       hold each for 4 s in turn
    servo_set.py --hold 8 0.55        hold for 8 s

Never commands the motor.
"""
import sys, time, struct
from vesc_packet import pkt, open_port, servo_us

args = sys.argv[1:]
hold = 4.0
if args and args[0] == '--hold':
    hold = float(args[1]); args = args[2:]
vals = [float(a) for a in args]
if not vals:
    print(__doc__); sys.exit(1)
for v in vals:
    if not 0.0 <= v <= 1.0:
        print(f"0.0~1.0 범위를 벗어남: {v}"); sys.exit(1)

s = open_port('/dev/ttyACM0')
for v in vals:
    print(f"servo = {v:.4f}  ({servo_us(v):.0f} us)  {hold:.0f}초 유지", flush=True)
    t0 = time.time()
    while time.time() - t0 < hold:
        s.write(pkt(bytes([12]) + struct.pack('>h', int(round(v * 1000)))))  # COMM_SET_SERVO_POS
        time.sleep(0.1)
print("끝 — 서보는 마지막 값에 머무릅니다 (중재기가 ESTOP/MANUAL 로 가면 STR_CENTER 로 복귀)")
