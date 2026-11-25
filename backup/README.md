# Board configs

> **Do not apply these to your board.** They are records from two specific
> vehicles. A VESC motor configuration belongs to a board-and-motor pair:
> `foc_motor_r`, `foc_motor_l` and `foc_motor_flux_linkage` are measurements of
> *that* motor through *that* board's current shunts. Loading someone else's
> values skips detection and can drive your motor badly.

They are published because the reasoning in each README is the part that
transfers — what was measured, what it was before, and why it changed. Use
them as worked examples alongside [`../docs/calibration.md`](../docs/calibration.md),
not as a starting configuration.

| Directory | Board | Notes |
|---|---|---|
| `nuc4_20261023/` | `60_MK6`, UUID `58 00 33 00 0F 50 4B 4D 31 38 31 20`, `controller_id` 72 | first board; also holds the pre-edit ROS configs |
| `local_id120_20261030/` | `60_MK6`, UUID `1B 00 36 00 18 47 31 33 38 35 32 38`, `controller_id` 120 | second board; includes the flashed image and post-apply readback |
| `hp_id75_20261125/` | `60_MK6_HP`, UUID `3B 00 24 00 0A 47 31 33 39 32 38 30`, `controller_id` 75 | the one HP board; arrived already running an older arbiter build |

Two are plain `60_MK6` and one is `60_MK6_HP`, which needs a different build
target — see [`../firmware/README.md`](../firmware/README.md).

## What the files are

| | |
|---|---|
| `AppConf.xml` | as the board was found — `app_to_use = 3` (`APP_UART`). The revert point. |
| `AppConf_arbiter.xml` | the same file with `app_to_use = 8` (`APP_CUSTOM`) and nothing else changed |
| `McConf.xml` | motor config, used unchanged by the arbiter |
| `after_*.xml`, `revert_*.xml` | readbacks taken after applying, kept as evidence the values survived |
| `60_mk6.bin` | the image actually flashed |

`--getCustomConf` produced nothing on either board, so there is no custom
config to back up. VESC Tool's CLI has no flag for downloading Lisp scripts,
so those are not captured either.

## Make your own before flashing

Flashing resets app and motor config to defaults — see
[`../firmware/README.md`](../firmware/README.md). Identify the board by UUID
rather than by device node, since `/dev/ttyACM0` is whichever board enumerated
first.
