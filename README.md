# VESC ELRS Arbiter

An ELRS/CRSF radio receiver wired **straight into a VESC**, with the VESC
firmware itself arbitrating between the human on the sticks and the autonomy
computer. No separate RC-to-PWM board, no second microcontroller, no ROS node
in the safety path.

Two halves that only work as a set:

| | |
|---|---|
| [`firmware/`](firmware/) | A custom VESC app (`app_arbiter.c`) plus a small patch to the VESC firmware. Reads CRSF, drives motor and servo, sends telemetry back. |
| [`radio/`](radio/) | An EdgeTX telemetry screen (`vesc.lua`) that shows what the car is actually doing. |

They share a wire protocol that is not CRSF-standard — the arbiter smuggles
steering, throttle and mode into stock CRSF telemetry fields because ELRS only
relays a handful of frames per second. **Flash one without updating the other
and the radio will display plausible nonsense.** See
[`docs/telemetry.md`](docs/telemetry.md) for the contract.

## Why it is built this way

On an F1TENTH-class car the usual setup puts the RC receiver on a separate
channel and lets a ROS node decide who drives. That means the emergency stop
depends on Linux, DDS, and a USB cable all being healthy. Here the decision
lives in the motor controller's own firmware, which is the last thing to die.

The arbiter is the **single writer** to the motor and servo. When it is driving,
`app_arbiter_blocks_comm()` returns true and the patched `commands.c` discards
the computer's motor and servo commands outright, so the two can never fight
over the output.

## Safety model

One 3-position switch selects the mode, and priority is strict:

```
RC link lost          ->  STOP          (highest priority, always)
SEL mid    = ESTOP    ->  STOP
SEL high   = MANUAL   ->  sticks drive
SEL low    = AUTO     ->  computer drives, if its commands are fresh
SEL low, computer stale -> STOP
```

Two choices worth calling out, because both are deliberate and both are the
opposite of what an RC car normally does:

**There is no fallback to the sticks while the switch is on AUTO.** If the
computer dies mid-lap the car stops and waits. It does not quietly hand control
back to whoever is holding the radio, because a stick knocked by a bag strap
must not move a car that someone believes is parked. Taking over is an explicit
act: flip the switch to MANUAL.

**No receiver means no driving at all — not even autonomous.** Losing the RC
link sets `arbiter_driving = true`, which blocks the computer's commands too. A
car that can be commanded but not stopped is worse than a car that cannot move,
so if you unplug the receiver, the board is inert until you either plug it back
in or switch the app away from Custom. This surprises people; it is the point.

The throttle control mode (current vs. speed) can only change while the
throttle stick is centred, so flipping it mid-drive cannot make the car lurch.

## Hardware

- VESC 6 MK6 (`60_mk6`) or MK6 HP (`60_mk6_hp`) — **pick the right target**, the
  two differ in current-shunt resistance (0.5 mΩ vs 0.3 mΩ) and an HP image on a
  plain board over-reports current by about 1.67x
- Any ExpressLRS receiver with a full-duplex UART (CRSF @ 420000 8N1)
- An EdgeTX handset
- A steering servo on the VESC's servo output

Wiring and channel setup: [`docs/wiring.md`](docs/wiring.md).

## Quick start

```bash
# 1. firmware
git clone --depth 1 --branch release_6_06 https://github.com/vedderb/bldc
cd bldc
git apply ../firmware/bldc_arbiter.patch
cp ../firmware/app_arbiter.c applications/
make arm_sdk_install          # pinned gcc-arm-none-eabi 7-2018-q2
make 60_mk6                   # or 60_mk6_hp

# 2. back up the board's config BEFORE flashing -- flashing resets it
vesc_tool --offscreen --vescPort /dev/ttyACM0 --getMcConf  McConf.xml
vesc_tool --offscreen --vescPort /dev/ttyACM0 --getAppConf AppConf.xml

# 3. flash, then put both configs back with app_to_use = 8 (APP_CUSTOM)
vesc_tool --offscreen --vescPort /dev/ttyACM0 --uploadFirmware build/60_mk6/60_mk6.bin
sed 's|<app_to_use>.*</app_to_use>|<app_to_use>8</app_to_use>|' AppConf.xml > AppConf_arbiter.xml
vesc_tool --offscreen --vescPort /dev/ttyACM0 --setMcConf  McConf.xml
vesc_tool --offscreen --vescPort /dev/ttyACM0 --setAppConf AppConf_arbiter.xml

# 4. radio
cp radio/vesc.lua /path/to/sdcard/SCRIPTS/TELEMETRY/

# 5. check it is running, then calibrate the steering for your car
python3 tools/vesc_term.py threads     # should list "RC Arbiter"
python3 tools/servo_tune.py            # mode switch on AUTO
```

**Calibrate the steering before driving.** The committed values are
measurements from one specific vehicle and will not centre yours. VESC Tool's
own servo slider cannot do this on a board running the arbiter — see
[`docs/calibration.md`](docs/calibration.md).

Details and pitfalls: [`firmware/README.md`](firmware/README.md),
[`radio/README.md`](radio/README.md).

## Layout

```
firmware/     app_arbiter.c, bldc_arbiter.patch
radio/        vesc.lua (EdgeTX telemetry screen)
tools/        Python scripts for identifying the board, reading arbiter state,
              and calibrating the steering
docs/         wiring, telemetry contract, calibration
backup/       per-board VESC config XML, one directory per board (UUID in its README)
prototypes/   early LispBM implementations, superseded -- kept for the notes in them
```

## Licence

GPL-3.0-or-later. `app_arbiter.c` and the patch are compiled into the VESC
firmware, which is GPL-3.0-or-later (Copyright Benjamin Vedder), so they
inherit it. `radio/vesc.lua` runs standalone on the handset but is released
under the same terms for consistency.
