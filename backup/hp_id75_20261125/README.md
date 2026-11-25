# VESC config backup — 2026-11-25 (HP board)

```
HW 60_MK6_HP          <- the only HP board of the three
UUID 3B 00 24 00 0A 47 31 33 39 32 38 30
controller_id 75
as found: FW 6.06, app_to_use 8 (APP_CUSTOM) -- the arbiter was already on it
```

This is the board the stale prebuilt `vesc_elrs_60_mk6_hp_fw6.06.bin` was built
for. It arrived already running an arbiter build from September, whose steering
centre was still the hardcoded `0.5`.

**It needs `make 60_mk6_hp`, not `make 60_mk6`.** A plain image on an HP board
assumes a larger shunt than it has and **under**-reports current by about 0.6x,
so the current limits trip late — the opposite direction, and more dangerous,
than the HP-image-on-plain-board mistake.

## Files

| File | Contents |
|---|---|
| `AppConf.xml` | as found. Already `app_to_use = 8`. |
| `AppConf_arbiter.xml` | identical (it was already 8); kept for symmetry with the other boards |
| `AppConf_uart.xml` | `app_to_use = 3` (`APP_UART`) — **made by hand**, since this board had no stock config to fall back to. Use it to disable the arbiter. |
| `McConf.xml` | motor config, used unchanged |
| `after_*.xml` | readback after applying, as evidence |
| `60_mk6_hp.bin` | the image finally flashed, md5 `67efb1e417a7ae135c2c69d11460c9d6` |

`--getCustomConf` produced nothing, so there is no custom config to back up.

## Steering calibration (2026-11-25)

Measured with `tools/servo_tune.py` at the stock 1000..2000 us output range:

```
centre 0.3040 = 1304 us
left   0.0160 = 1016 us    288 us of travel
right  0.7000 = 1700 us    396 us of travel
```

Both limits are mechanical — the linkage binds. Left **asymmetric** on purpose:
the firmware supports per-side gains, and clipping would throw away 108 us of
right-hand steering. If this car is ever driven by ROS, clip `SERVO_MAX` to
`0.5920` first, because a single steering gain cannot express this range.

### The output range was widened and then reverted

The left limit at 1016 us sits only **16 us** above the 1000 us output floor, so
it initially looked as though the floor was the constraint.
`SERVO_OUT_PULSE_MIN_US` was lowered to 800, the firmware rebuilt and flashed,
and the car re-measured — which showed the real limit was mechanical and the
change bought 16 us. It was reverted to stock, and the three constants above are
in the stock mapping.

Lesson recorded in [`../../docs/calibration.md`](../../docs/calibration.md):
convert marks to microseconds before blaming the output range.

## Apply / revert

```bash
V=vesc_tool; P=/dev/ttyACM0
$V --offscreen --vescPort $P --uploadFirmware 60_mk6_hp.bin
# flashing resets both configs -- always re-apply
$V --offscreen --vescPort $P --setMcConf  McConf.xml
$V --offscreen --vescPort $P --setAppConf AppConf_arbiter.xml
# to disable the arbiter instead:
$V --offscreen --vescPort $P --setAppConf AppConf_uart.xml
```
