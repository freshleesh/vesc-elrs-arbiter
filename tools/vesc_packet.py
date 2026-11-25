# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal VESC binary packet client (start, len, payload, CRC16/XMODEM, stop)."""
import serial, struct, time

def crc16(d):
    c = 0
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c

def pkt(p):
    h = bytes([2, len(p)]) if len(p) <= 255 else bytes([3]) + struct.pack('>H', len(p))
    return h + p + struct.pack('>H', crc16(p)) + b'\x03'

def frames(buf):
    """Pop complete, CRC-valid frames off a bytearray, yielding payloads.

    Payloads routinely contain 0x02 and 0x03, so a start byte alone is not a
    frame boundary -- the CRC is what confirms it. On a bad CRC we advance one
    byte and try again rather than trusting the next 0x02 we see.
    """
    while True:
        if not buf:
            return
        b = buf[0]
        if b == 2:
            hdr = 2
            if len(buf) < 2: return
            ln = buf[1]
        elif b == 3:
            hdr = 3
            if len(buf) < 3: return
            ln = struct.unpack('>H', bytes(buf[1:3]))[0]
        else:
            del buf[0]
            continue
        end = hdr + ln + 3
        if len(buf) < end:
            return
        pl = bytes(buf[hdr:hdr + ln])
        crc_rx = struct.unpack('>H', bytes(buf[hdr + ln:hdr + ln + 2]))[0]
        if buf[end - 1] == 3 and crc_rx == crc16(pl):
            del buf[:end]
            if pl:
                yield pl
        else:
            del buf[0]

def open_port(dev='/dev/ttyACM0', timeout=0.2):
    return serial.Serial(dev, 115200, timeout=timeout)

# Servo output mapping, mirroring conf_general.h. These are the stock VESC
# values; if the firmware on the board was built with a different pulse range,
# change them to match or every microsecond readout here will be wrong.
SERVO_PULSE_MIN_US = 1000
SERVO_PULSE_MAX_US = 2000

def servo_us(pos):
    return SERVO_PULSE_MIN_US + pos * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US)
