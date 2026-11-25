# nuc4 VESC 설정 백업 — 2026-10-23

대상 보드 (nuc4 `/dev/ttyACM0`):

```
VESC Firmware Version 6.5, Hardware: 60_MK6
UUID: 58 00 33 00 0F 50 4B 4D 31 38 31 20
```

ELRS 중재기 펌웨어로 바꾸기 **전** 상태입니다. 원래 작업했던 보드는 `60_MK6_HP`라
펌웨어 타겟이 다릅니다 — 이 보드에는 `make 60_mk6` 쪽을 써야 합니다.

## 파일

| 파일 | 내용 |
|---|---|
| `AppConf.xml` | 원본 그대로. `app_to_use=3` (APP_UART) — **원복용** |
| `AppConf_arbiter.xml` | 위에서 `app_to_use`만 `8` (APP_CUSTOM)로. 다른 필드 전부 동일 (22행 1줄 차이) |
| `McConf.xml` | 모터 설정 원본. 중재기로 바꿔도 **그대로 사용** |

`servo_out_enable`은 원본에서 이미 `1`이라 손대지 않았습니다.
Custom config는 비어 있어서(`--getCustomConf`가 파일을 안 만듦) 백업 대상이 없습니다.
Lisp 스크립트는 vesc_tool CLI에 내려받기 플래그가 없어 백업하지 못했습니다.

## ⚠ 펌웨어를 구우면 설정이 초기화된다

`--uploadFirmware` 후 보드는 앱 설정과 모터 설정을 **기본값으로 되돌린다**
(실측: `app_to_use`가 3으로, 모터 설정도 변경됨). 같은 6.06 → 6.06 재플래시에서도
일어난다. 그래서 굽고 나면 **항상** 아래 순서로 둘 다 다시 넣어야 한다.

## 적용

nuc4에서 (`~/vesc_tool_6.05` 또는 6.06):

```bash
V=./vesc_tool_6.06; P=/dev/ttyACM0
$V --offscreen --vescPort $P --uploadFirmware 60_mk6.bin
# 재부팅 대기 후
$V --offscreen --vescPort $P --setMcConf  McConf.xml
$V --offscreen --vescPort $P --setAppConf AppConf_arbiter.xml
```

## 원복

```bash
$V --offscreen --vescPort $P --setAppConf AppConf.xml   # UART 앱으로 복귀
$V --offscreen --vescPort $P --setMcConf  McConf.xml
```

펌웨어까지 되돌리려면 stock 6.05/6.06을 따로 구해야 합니다 (이 백업에는 펌웨어 이미지가 없음).

## 조향 교정값은 펌웨어에 있다

중재기가 직접 서보를 쓰므로 ROS `vesc_driver`의 오프셋이 적용되지 않는다.
`firmware/app_arbiter.c` 상단 상수가 차량 교정값을 그대로 들고 있어야 한다:

```
STR_CENTER  0.6263f   직진 서보 위치 (2026-10-23 눈으로 측정)
SERVO_MIN   0.2526f   = STR_CENTER - 0.3737
SERVO_MAX   1.0f      출력 상한 (2000us). 우측은 기계적 스톱이 아니라
                      출력 범위가 한계였다 -- 1.0 까지 걸림 없이 돌아갔다.
```

중앙이 서보 중점(0.5)에서 126us 치우쳐 있어서 우측 여유가 좁다. 1.0 까지 열어도
우측은 0.3737 뿐이므로, 좌측을 같은 값으로 줄여 **좌우 대칭 ±0.3737** 로 맞췄다
(`STR_RANGE_L` / `STR_RANGE_R`). ROS 는 조향에 단일 게인
(`steering_angle_to_servo_gain`)만 쓰므로 비대칭 범위를 표현할 수 없고,
비대칭으로 두면 수동 주행과 AUTO 의 조향 감각이 달라진다.

**차를 바꾸면 이 상수를 그 차 값으로 고치고 재빌드해야 한다.**

## ROS 쪽도 같이 맞춰야 한다 (2026-10-23 적용함)

AUTO 에서는 ROS 가 서보를 직접 쓰므로 `STR_CENTER` 가 적용되지 않는다.
nuc4 의 아래 두 파일을 같은 값으로 바꿨다 (원본은 `ros_config_before/` 와
각 파일의 `.bak_20261023`):

| 파일 | 값 |
|---|---|
| `stack_master/config/CAR/vehicle_config.yaml` | `steering_angle_to_servo_offset` 0.5304 → **0.6263**, `servo_min` 0.0 → 0.2526 |
| `sensor/vesc/vesc_driver/params/vesc_config.yaml` | `servo_min` 0.15 → **0.2526**, `servo_max` 0.85 → **1.0** |

실제로 조향을 자르는 건 `vesc_driver` 쪽 값이다. `install/` 은 symlink 이므로
colcon 재빌드 없이 바로 반영된다. `SIM/vehicle_config.yaml` 은 시뮬레이션용이라
0.5304 그대로 두었다.
