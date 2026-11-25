# Tools

Small Python scripts that speak the VESC binary protocol directly. They exist
because VESC Tool's CLI has no flag for terminal commands, and its GUI servo
slider cannot hold a position against the arbiter (see below).

Only `pyserial` is needed.

| | |
|---|---|
| `vesc_packet.py` | the shared protocol module — framing, CRC16/XMODEM, and the servo microsecond mapping |
| `fwver.py` | firmware version, hardware name and UUID. **Run this first**: boards are told apart by UUID, not by device node. |
| `vesc_term.py` | send a terminal command and print the replies (`arb`, `threads`, `help`, ...) |
| `servo_read.py` | sample the arbiter's 50 Hz feedback: mode, servo position, stick positions |
| `servo_set.py` | hold the servo at given positions for a few seconds each |
| `servo_tune.py` | **interactive steering calibration** — arrow keys, with marks for centre and limits |

```bash
python3 tools/fwver.py
python3 tools/vesc_term.py arb 3
python3 tools/servo_read.py
python3 tools/servo_tune.py
```

All of them default to `/dev/ttyACM0`. None ever commands the motor.

## Why not just use VESC Tool

Two things it cannot do on a board running the arbiter:

**Terminal commands.** The CLI has no flag for them. `vesc_term.py` sends a
`COMM_TERMINAL_CMD` (id 20) and prints the `COMM_PRINT` (id 21) replies, which
is how you check that the arbiter is actually running — `threads` should list
`RC Arbiter`, and `arb` should print state rather than `Invalid command: arb`.

**Holding a servo position.** VESC Tool's slider sends one command per change,
and the arbiter reclaims the servo after `CMD_TIMEOUT_MS` (500 ms) of silence
from the computer. So the servo jumps back to `STR_CENTER` the instant you stop
dragging, which looks like the radio and the tool fighting. `servo_tune.py` and
`servo_set.py` resend at 10 Hz so `cmd_fresh` stays true.

Both need the mode switch on **AUTO** with the RC link up. Otherwise the arbiter
is driving, `app_arbiter_blocks_comm()` returns true, and every motor and servo
command from the computer is discarded on arrival.

Only one program can hold the port. If a script reports
`Device or resource busy`, disconnect VESC Tool — the servo stays where it was
last commanded, so nothing is lost.

## Note on `vesc_packet.py`

Frame resynchronisation is by **CRC**, not by scanning for the next start byte.
Payloads routinely contain `0x02` and `0x03`, so a start byte alone is not a
frame boundary; trusting it drops valid frames and occasionally invents one.

`SERVO_PULSE_MIN_US` / `MAX_US` mirror `conf_general.h`. If you build firmware
with a different pulse range, change them to match or every microsecond readout
here will be wrong.
