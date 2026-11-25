#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print a VESC's firmware version, hardware name and UUID (COMM_FW_VERSION)."""
import sys, time
from vesc_packet import pkt, frames, open_port

dev = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
s = open_port(dev)
s.write(pkt(bytes([0])))
time.sleep(0.5)
buf = bytearray(s.read(512))
for pl in frames(buf):
    if pl[0] != 0:
        continue
    major, minor = pl[1], pl[2]
    rest = pl[3:]
    name = rest.split(b'\x00')[0].decode(errors='replace')
    uuid = rest[len(name) + 1:][:12]
    print(f"FW {major}.{minor:02d}   HW '{name}'")
    print("UUID:", ' '.join(f'{b:02X}' for b in uuid))
    break
else:
    print("응답 없음 — 포트가 다른 프로그램에 잡혀 있는지 확인"); sys.exit(1)
