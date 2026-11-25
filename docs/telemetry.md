# The contract between firmware and radio

ELRS relays only a few telemetry frames per second (its Telem Ratio setting),
so the arbiter does not send one frame per quantity. It packs everything that
changes fast into the smallest frames CRSF has, and reuses stock sensor fields
for things they were never meant to carry.

**This is why the two halves of this repo have to ship together.** Nothing in
the wire format announces its real meaning; `vesc.lua` just knows.

## What the VESC sends

| CRSF frame | Period | Field | Stock meaning | What the arbiter puts there |
|---|---|---|---|---|
| Vario `0x07` | 50 ms | `VSpd` | vertical speed, 0.01 m/s | **vehicle speed**, m/s |
| Battery `0x08` | 50 ms in AUTO, else 500 ms | `RxBt` | voltage, 0.1 V | speed m/s x 10 |
| | | `Curr` | current, 0.1 A | **throttle %** x 10, signed, -100.0..100.0 |
| | | `Capa` | capacity, mAh | max speed, cm/s |
| | | `Bat%` | percent, int8 | **steering %**, signed, -100..100 |
| Flight Mode `0x21` | on change + 1000 ms | `FM` | mode string | `"<AUTO\|ESTOP\|MANUAL> <CUR\|SPD>"` |

Vario is chosen for speed because at 6 bytes it is the smallest CRSF frame, so
it fits through the link most often.

The battery frame slows to 500 ms outside AUTO on purpose: in MANUAL and ESTOP
the handset already knows the stick positions locally and does not need them
echoed back over a slow link.

## What the radio computes locally instead

Anything the handset already knows is read from its own mixer, not the air —
instant, and it costs no telemetry bandwidth:

- mode switch position, from `sa`
- steering stick, from `ch1`
- throttle stick, from `ch3`
- max-speed dial, from `ch7`

The one thing it cannot know locally is the mode the arbiter *actually settled
on*, which is why `FM` is sent at all. Two cases differ from the switch
position:

- **`NOPC`** — the switch says AUTO but the computer's commands are stale, so
  the car is stopped. `vesc.lua` renders this as its own mode name; there is no
  `NOPC` in the firmware. It is the Lua script's label for
  `sw == "AUTO" and fm_mode == "ESTOP"`.
- **`CUR` / `SPD`** — the throttle control mode is toggled by a switch the
  firmware reads directly, so the handset learns the result from `FM`.

## Channel numbering: the off-by-one that will bite you

The firmware counts CRSF channels from 0. EdgeTX counts its own mixer channels
from 1. They refer to the same wires:

| Purpose | `app_arbiter.c` | EdgeTX mixer | Suggested source |
|---|---|---|---|
| Steering | `CH_STEER 0` | CH1 | Ail stick |
| Throttle | `CH_THR 2` | CH3 | Thr stick |
| Mode select (3-pos) | `CH_SEL 5` | CH6 | SA |
| Max speed dial | `CH_MAXSPD 6` | CH7 | P2 / S1 |
| Control mode | `CH_CTRL 7` | CH8 | SD |

`vesc.lua` uses the EdgeTX numbers (`ch1`, `ch3`, `ch7`) because it runs on the
handset. The firmware uses the CRSF numbers. Both are correct; they are the same
five channels.

## Custom app data back to the computer

Separately from the radio, the arbiter emits a `COMM_CUSTOM_APP_DATA` packet at
50 Hz over the USB/UART comm link so the autonomy stack can see what the driver
is doing:

```
byte 0    0xA5        magic
byte 1    mode        current arbiter mode
int16     servo  x1000
int16     rc_steer    x1000
int16     rc_throttle x1000
```

## Changing any of this

If you add or move a field, change both sides in the same commit. The constants
that must agree are duplicated deliberately, with a comment saying so:

- `MAXV_MIN`, `MAXV_MAX`, `DEADBAND` — in `app_arbiter.c` and at the top of
  `vesc.lua`
- the steering centre — in `app_arbiter.c` and, if you run ROS, in the
  autonomy stack's own config (see [calibration.md](calibration.md))
