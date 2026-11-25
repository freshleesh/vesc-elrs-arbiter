# Steering calibration

**The committed values are measurements from one specific car and will not
centre yours.** They live in `app_arbiter.c`:

```c
#define STR_CENTER     0.3040f  // straight ahead
#define SERVO_MIN      0.0160f  // left travel limit
#define SERVO_MAX      0.7000f  // right travel limit
#define STR_RANGE_L    (STR_CENTER - SERVO_MIN)  // 0.2880
#define STR_RANGE_R    (SERVO_MAX - STR_CENTER)  // 0.3960
```

Servo output is `0.0 .. 1.0` mapped to `1000 .. 2000 us` at 50 Hz
(`SERVO_OUT_PULSE_MIN_US` / `MAX_US` / `SERVO_OUT_RATE_HZ` in `conf_general.h`),
and `pwm_servo.c` truncates anything outside `0.0 .. 1.0`.

## Why the centre is not 0.5

The servo horn, the linkage and the rack all have offsets, so straight-ahead is
wherever it happens to be — never the servo midpoint. That matters because the
offset eats one side's travel: with the centre at 0.3040 there is only 0.3040
below it and 0.6960 above.

So each side gets its own gain, `STR_RANGE_L` and `STR_RANGE_R`. Full stick
reaches that side's limit and no further, in both directions, whatever the
offset is.

## Measure it with tools/servo_tune.py

```bash
python3 tools/servo_tune.py
```

Arrow keys nudge the servo; `m`, `l` and `r` mark the centre and the two
limits; `q` prints the three `#define` lines.

**VESC Tool's own servo slider cannot do this job on a board running the
arbiter.** The slider sends one command per change, and the arbiter reclaims
the servo after `CMD_TIMEOUT_MS` (500 ms) of silence from the computer — so the
servo snaps back to `STR_CENTER` the moment you stop dragging, which looks
exactly like the two fighting over the output. `servo_tune.py` resends at 10 Hz,
which keeps `cmd_fresh` true and holds position.

Either way the mode switch must be on **AUTO** with the RC link up, or the
arbiter is driving and every comm command is discarded.

Procedure:

1. Put the car on blocks with the drive wheels clear. Calibration never
   commands the motor, but you are flashing firmware.
2. Flash the arbiter and apply the configs (see [`../firmware/README.md`](../firmware/README.md)).
3. Find straight-ahead, mark it with `m`.
4. Step outward each way until the linkage binds or the servo starts buzzing.
   **Buzzing means the servo is pushing against something it cannot move** — it
   draws current and chews the gears, so back off and mark just inside it.
5. Put the three values in `app_arbiter.c`, rebuild, reflash, re-apply configs.
6. Check by eye that the wheels are straight at centre.

## Asymmetric or symmetric?

The firmware handles an asymmetric range natively, so **if the radio is the only
thing steering, keep it asymmetric** and use every degree the car has.

Clip to symmetric only if a ROS stack will also drive the servo. ROS steers with
a single gain (`steering_angle_to_servo_gain`) and cannot express two different
ranges, so an asymmetric car would steer differently under the driver than under
the computer — which makes any handling comparison meaningless.

The committed car is left asymmetric (288 us left, 396 us right). To make it
symmetric you would set `SERVO_MAX` to `0.5920` and lose 108 us of right-hand
steering.

## If you run ROS as well

In AUTO the stack drives the servo itself, so `STR_CENTER` does **not** apply —
the stack has its own copy of the centre. Both must change together.

For an F1TENTH / `unicorn-racing-stack` style layout:

| File | Key | Set to |
|---|---|---|
| `stack_master/config/CAR/vehicle_config.yaml` | `steering_angle_to_servo_offset` | `STR_CENTER` |
| | `servo_min` | `SERVO_MIN` |
| `vesc_driver/params/vesc_config.yaml` | `servo_min` | `SERVO_MIN` |
| | `servo_max` | `SERVO_MAX` |

The values that actually clip the steering are the **`vesc_driver`** ones; the
`vehicle_config` offset only shifts the centre. Defaults of `0.15` / `0.85` are
narrower than most cars' real travel and are easy to mistake for a mechanical
limit.

Leave the simulation config alone — it is not describing your car.

With `--symlink-install`, the installed YAML files are symlinks into `src/`, so
editing them takes effect without rebuilding.

## Do not widen the output pulse range to gain travel

It is tempting, when a limit turns out to be the `1000 us` output floor rather
than the car, to lower `SERVO_OUT_PULSE_MIN_US`. This was tried and reverted,
and the reasoning is worth keeping:

- It **rescales the whole `0..1` mapping**, so every calibration number —
  firmware and ROS alike — has to be measured again.
- `conf_general.h` is a patched file, so the patch grows a second hook.
- It drives the servo outside its rated range, where it can buzz against its own
  internal stops.
- And on the car measured here it bought **16 us**. The left limit sits at
  1016 us: the real constraint was mechanical all along, only barely past the
  floor.

Convert your marks to microseconds before concluding the output range is the
problem. `servo_tune.py` prints both. If the limit really is the floor, the
honest fix is mechanical — move the servo horn a tooth or adjust the linkage so
straight-ahead sits nearer the middle of the range, which recovers travel on
both sides and needs no firmware change at all.

## Worked examples

[`../backup/`](../backup/) holds the real configs from three boards, one
directory each, with the measured values and the reasoning in their READMEs.
Board identity is by UUID, not by device node — see
[`../firmware/README.md`](../firmware/README.md).
