# Radio side

`vesc.lua` is an EdgeTX **telemetry screen** — not firmware. Nothing on the
handset itself is modified or reflashed.

## Install

Copy it onto the handset's SD card:

```
/SCRIPTS/TELEMETRY/vesc.lua
```

Then in the model's **Display** (telemetry screens) page, set a screen to type
`Script` and pick `vesc`.

The filename must stay `vesc.lua`: EdgeTX telemetry script names are limited to
six characters.

Easiest transfer is to plug the handset in over USB and let it present the SD
card as mass storage.

## What it shows

```
 MANUAL CUR                    2.4m/s
 SPD   2.41  [########------------]
 THR     35% [--------|####-------]
 STR    -12% [-----##|------------]
 MAXV  4.25  [##########----------]
```

- **Top left** — the mode the car actually settled on, which is not always the
  switch position. `NOPC` means the switch is on AUTO but the computer's
  commands have gone stale, so the car is stopped.
- **Next to it** — `CUR` or `SPD`, the throttle control mode.
- **Top right** — speed, straight from the VESC.
- **THR / STR** — centre-origin bars. In MANUAL they are your sticks, read
  locally and so instant; in AUTO they are what the computer is commanding,
  which arrives over the air.
- **MAXV** — the dial's max-speed setting, computed locally.

## Channel requirements

The script reads `sa`, `ch1`, `ch3` and `ch7` from the handset's own mixer, so
those sources must be on those channels — see
[`../docs/wiring.md`](../docs/wiring.md). If the display disagrees with the car,
suspect the mixer before the firmware.

If you put the mode switch somewhere other than SA, change `num("sa")` in the
script.

## Keep in sync with the firmware

Three constants are duplicated from `app_arbiter.c` and are marked as such at
the top of the file:

```lua
local MAXV_MIN = 0.5
local MAXV_MAX = 8.0
local DEADBAND = 0.05
```

More importantly, the whole display depends on the arbiter packing its data
into stock CRSF telemetry fields. The wire format is documented in
[`../docs/telemetry.md`](../docs/telemetry.md). Change one side without the
other and the screen shows plausible, wrong numbers.

## If the numbers are stale or frozen

- Check ExpressLRS **Telem Ratio**. The arbiter offers updates every 50 ms; a
  low ratio drops most of them.
- `getValue()` returns 0 for a sensor the handset has never seen. If a field
  reads 0 forever, the handset has not discovered it — delete and re-discover
  the sensors in the model's Telemetry page.
- In MANUAL and ESTOP the battery frame deliberately slows to 500 ms. THR and
  STR still look instant there because they are read locally, not received.
