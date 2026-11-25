# Steering calibration

**The committed values are for one specific car and will not centre yours.**
They live in `app_arbiter.c`:

```c
#define STR_CENTER     0.6263f  // servo position for straight ahead
#define SERVO_MIN      0.2526f  // left travel limit
#define SERVO_MAX      1.0f     // right travel limit (output ceiling, 2000 us)
#define STR_RANGE_L    (STR_CENTER - SERVO_MIN)
#define STR_RANGE_R    (SERVO_MAX - STR_CENTER)
```

Servo output is `0.0 .. 1.0` mapped to `1000 .. 2000 us` at 50 Hz
(`SERVO_OUT_PULSE_MIN_US` / `MAX_US` / `SERVO_OUT_RATE_HZ` in `conf_general.h`).

## Why the centre is not 0.5

On a real car the servo horn, the linkage and the rack all have offsets, so
straight-ahead is wherever it happens to be. The car this was calibrated on
centres at **0.6263** — 126 us off the servo midpoint.

That asymmetry matters because it eats one side's travel. With the centre at
0.6263 the right side has only `1.0 - 0.6263 = 0.3737` before the output range
runs out, while the left side would have `0.6263`. So each side gets its own
gain (`STR_RANGE_L` / `STR_RANGE_R`) and full stick reaches that side's limit
without ever exceeding it.

## Why the two sides were then made equal

Measuring the car above, the servo reached 1.0 to the right **without hitting a
mechanical stop** — the limit was the output ceiling, not the steering rack. The
left side was therefore reduced from 0.4763 to 0.3737 to match, giving a
symmetric +/-0.3737.

That is a deliberate loss of left-hand steering angle, for one reason: a ROS
autonomy stack steers with a **single gain**
(`steering_angle_to_servo_gain`) and cannot express an asymmetric range. Left
asymmetric, the car would steer differently under the driver than under the
computer, which makes every handling comparison meaningless.

If you do not run such a stack, keep the asymmetry and let each side use its
full travel.

## Procedure

1. Put the car on blocks with the drive wheels clear. Steering calibration
   never commands the motor, but you are about to flash firmware.
2. Flash the arbiter and apply the configs (see [`../firmware/README.md`](../firmware/README.md)).
3. With the mode switch on AUTO, the computer's servo commands pass through, so
   you can sweep the servo with `COMM_SET_SERVO_POS` (id 12) packets and watch
   the wheels. Step in 0.01 until they point straight — that value is
   `STR_CENTER`.
4. Keep stepping outward each way until the wheels stop moving or the linkage
   binds. Those are `SERVO_MIN` and `SERVO_MAX`. If you reach 0.0 or 1.0 first,
   the output range is your limit, not the car.
5. Edit the three constants, rebuild, reflash, re-apply the configs.
6. Check by eye that the wheels are straight at centre and that both sides
   deflect about equally.

Reading the arbiter's `COMM_CUSTOM_APP_DATA` stream (see
[telemetry.md](telemetry.md)) is a quicker way to do step 3: it reports the
servo position at 50 Hz, so you can hold the sticks where the wheels look
straight and read the number off directly instead of guessing percentages.

## If you run ROS as well

In AUTO the autonomy stack drives the servo itself, so `STR_CENTER` does **not**
apply — the stack has its own copy of the centre. Both must change together or
manual and autonomous driving will steer differently.

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

If you built with `--symlink-install`, the installed YAML files are symlinks
into `src/`, so editing them takes effect without rebuilding.

## Worked examples

[`../backup/`](../backup/) holds the real configs from two boards, one directory
each, with the measured values and the reasoning in their READMEs. Board
identity is by UUID, not by which USB port it showed up on — see
[`../firmware/README.md`](../firmware/README.md) for how to read it.
