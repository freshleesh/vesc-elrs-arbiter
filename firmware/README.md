# Firmware

Two files, applied on top of an unmodified VESC firmware tree:

| File | What it does |
|---|---|
| `app_arbiter.c` | The arbiter itself. Dropped into `applications/`, pulled in via `APP_CUSTOM_TO_USE`. |
| `bldc_arbiter.patch` | Three small edits: declarations in `applications/app.h`, the comm-command gate in `comm/commands.c`, and `APP_CUSTOM_TO_USE` in `conf_general.h`. |

The upstream tree is **not** vendored or submoduled here. The whole change is
one file and ~50 patched lines; carrying a fork would obscure that.

## Build

```bash
git clone --depth 1 --branch release_6_06 https://github.com/vedderb/bldc
cd bldc
git apply ../firmware/bldc_arbiter.patch
cp ../firmware/app_arbiter.c applications/
make arm_sdk_install     # downloads the pinned toolchain into tools/, no sudo
make 60_mk6              # or 60_mk6_hp
```

`make/tools.mk` pins **gcc-arm-none-eabi 7-2018-q2-update**. A distro
toolchain (13.x on current Ubuntu) is not a drop-in substitute. `make
arm_sdk_install` fetches the right one locally and needs no root.

There is no `6.06` tag upstream, only `release_6_0X` branches.

### Picking the target

`HW60_IS_HP` in `hwconf/trampa/vesc6/hw_60_core.h` changes:

| | `60_mk6` | `60_mk6_hp` |
|---|---|---|
| `CURRENT_SHUNT_RES` | 0.0005 | 0.0003 |
| `HW_LIM_CURRENT` | +/-120 A | +/-160 A |
| absolute max | 160 A | 240 A |

**An HP image on a plain MK6 over-reports current by about 1.67x.** It will not
destroy the board, but the current limits trip early, the motor feels gutless,
and every calibration is wrong. Read the board's own reported hardware name
before choosing — do not go by what the previous image was built for.

Flash usage is tight (around 94% of flash2, ram4 over 99%). Adding much to the
arbiter may not link.

## Identify the board first

Boards are told apart by UUID, not by USB port. Send a `COMM_FW_VERSION` (id 0)
packet; the reply carries firmware version, hardware name and UUID:

```
FW 6.06   HW '60_MK6'
UUID 1B 00 36 00 18 47 31 33 38 35 32 38
```

Motor calibration belongs to a board-and-motor pair, so **never apply one
board's `McConf.xml` to another**.

## Flash and configure

```bash
V=vesc_tool; P=/dev/ttyACM0

# back up first -- see below
$V --offscreen --vescPort $P --getMcConf  McConf.xml
$V --offscreen --vescPort $P --getAppConf AppConf.xml

$V --offscreen --vescPort $P --uploadFirmware build/60_mk6/60_mk6.bin

# then ALWAYS re-apply both
$V --offscreen --vescPort $P --setMcConf  McConf.xml
$V --offscreen --vescPort $P --setAppConf AppConf_arbiter.xml
```

### Flashing resets the configuration

`--uploadFirmware` returns the board to **default** app and motor settings. This
happens even on a 6.06 -> 6.06 reflash of the same image. Back up before
flashing and re-apply both configs after, every time.

### `app_to_use` must be 8

`APP_CUSTOM` is **8** in the `app_use` enum (`datatypes.h`), counting from
`APP_NONE = 0`. Seven is `APP_NRF`.

Set it with a one-line edit of the backed-up XML, which keeps every other field
exactly as the board had it:

```bash
sed 's|<app_to_use>.*</app_to_use>|<app_to_use>8</app_to_use>|' \
    AppConf.xml > AppConf_arbiter.xml
```

`servo_out_enable` must be `1`.

Get this wrong and the failure is **silent**: the firmware boots, VESC Tool
connects, and nothing complains — the arbiter thread simply never starts. To
confirm it is running:

- `threads` lists a thread named **`RC Arbiter`**
- `arb` prints state instead of `Invalid command: arb`
- `COMM_CUSTOM_APP_DATA` packets arrive at ~50 Hz

### Going back to stock behaviour

Re-apply the untouched `AppConf.xml` (`app_to_use = 3`, `APP_UART`). The arbiter
firmware can stay flashed: with the app switched away, the arbiter thread never
starts, `arbiter_driving` stays at its initial `false`, and the comm gate never
blocks anything. Behaviour matches stock.

`backup/` in this repo keeps, per board, the original config, the
`app_to_use = 8` variant, and the readback taken after applying them.

## Upgrading from 6.05

Expect these harmless differences when a 6.05 config is loaded into 6.06:

- `ConfigVersion` 3 -> 4
- new fields `foc_hfi_amb_*`, `foc_overmod_factor`
- `foc_offsets_cal_on_boot` renamed to `foc_offsets_cal_mode`, value preserved
- `foc_offsets_current/voltage` shift slightly — they are re-measured at boot
- `motor_brand` / `motor_model` blanked

VESC Tool warns that the config came from different firmware and suggests
re-running the wizard. Compare the functional values (current limits, FOC
parameters, pole count, gear ratio, wheel diameter) rather than trusting the
warning; they survive.

## Tunable constants

All at the top of `app_arbiter.c`. The ones you are most likely to touch:

| | |
|---|---|
| `STR_CENTER`, `SERVO_MIN`, `SERVO_MAX` | **per car** — see [`../docs/calibration.md`](../docs/calibration.md) |
| `CTRL_SW_MOMENTARY` | `1` push button, `0` latching switch |
| `THR_MAX` | current-rel cap at full stick, default 0.60 |
| `MAXV_MIN` / `MAXV_MAX` | dial range in m/s — **also in `vesc.lua`** |
| `DEADBAND` | **also in `vesc.lua`** |
| `SPEED_TO_ERPM_GAIN` | per drivetrain; same number as `vesc_driver`'s `speed_to_erpm_gain` |
| `RC_TIMEOUT_MS` / `CMD_TIMEOUT_MS` | 100 / 500 ms |

Speed control uses the VESC's own speed PID — tune it in VESC Tool under Motor
Settings -> PID Controllers, not here.
