#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Interactive steering tuner for the VESC ELRS arbiter.

Resends COMM_SET_SERVO_POS at 10 Hz so the arbiter's CMD_TIMEOUT_MS never
expires -- which is exactly what VESC Tool's own servo slider cannot do, since
it only sends on change and the arbiter reclaims the servo after 500 ms of
silence.

Requires the mode switch on AUTO with the RC link up; otherwise the arbiter is
driving and every comm command is discarded.

    left / right arrow   -0.001 / +0.001
    down / up arrow      -0.010 / +0.010
    c                    jump to the centre you have marked (or 0.5)
    m                    mark the current value as centre
    l / r                mark the current value as the left / right limit
    p                    print the marks so far
    q                    quit (prints a summary)

Never commands the motor.
"""
import sys, time, struct, termios, tty, select, os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vesc_packet import pkt, open_port, servo_us

DEV = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
pos = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
marks = {}

def clamp(v):
    return max(0.0, min(1.0, v))

def summary():
    if not marks:
        return "  (아무것도 표시하지 않았습니다)"
    out = []
    c = marks.get('center')
    for k, label in (('center', 'STR_CENTER'), ('left', 'SERVO_MIN'), ('right', 'SERVO_MAX')):
        if k in marks:
            out.append(f"  #define {label:<13} {marks[k]:.4f}f")
    if c is not None and 'left' in marks and 'right' in marks:
        l, r = c - marks['left'], marks['right'] - c
        out.append(f"  // 좌 {l:.4f} / 우 {r:.4f}" +
                   ("  <- 비대칭, ROS 를 쓰면 작은 쪽으로 맞출 것" if abs(l - r) > 0.005 else "  (대칭)"))
    return "\n".join(out)

s = open_port(DEV)
fd = sys.stdin.fileno()
old = termios.tcgetattr(fd)
print(__doc__)
print("시작 — SA 가 AUTO 가 아니면 아무 반응이 없습니다.\n")
try:
    tty.setcbreak(fd)
    last_draw = 0.0
    while True:
        s.write(pkt(bytes([12]) + struct.pack('>h', int(round(pos * 1000)))))

        if time.time() - last_draw > 0.1:
            marked = " ".join(f"{k[0]}={v:.4f}" for k, v in marks.items())
            sys.stdout.write(f"\rservo {pos:.4f}  ({servo_us(pos):4.0f} us)   {marked}        ")
            sys.stdout.flush()
            last_draw = time.time()

        if select.select([sys.stdin], [], [], 0.1)[0]:
            ch = sys.stdin.read(1)
            if ch == '\x1b':
                seq = sys.stdin.read(2)
                pos = clamp(pos + {'[C': 0.001, '[D': -0.001, '[A': 0.010, '[B': -0.010}.get(seq, 0.0))
            elif ch == 'm':
                marks['center'] = pos; print(f"\n  center = {pos:.4f}")
            elif ch == 'l':
                marks['left'] = pos;   print(f"\n  left   = {pos:.4f}")
            elif ch == 'r':
                marks['right'] = pos;  print(f"\n  right  = {pos:.4f}")
            elif ch == 'c':
                pos = marks.get('center', 0.5)
            elif ch == 'p':
                print("\n" + summary())
            elif ch in ('q', '\x03'):
                break
finally:
    termios.tcsetattr(fd, termios.TCSADRAIN, old)
    print("\n\n측정값:")
    print(summary())
    print("\napp_arbiter.c 에 넣고 재빌드 후 재플래시 (설정 재적용 잊지 마세요).")
