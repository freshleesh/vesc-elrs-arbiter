# VESC 설정 백업 — 2026-10-30 (두 번째 보드)

대상 보드 (작업 PC `/dev/ttyACM0`, USB 직결):

```
HW 60_MK6  (HP 아님)
UUID 1B 00 36 00 18 47 31 33 38 35 32 38
controller_id 120
플래시 전 FW 6.05 / app_to_use 3 (APP_UART)
```

nuc4 보드(`backup/nuc4_20261023/`, UUID `58 00 33 ...`, id 72)와 **다른 보드**다.
설정 XML을 서로 섞어 쓰면 안 된다.

## 파일

| 파일 | 내용 |
|---|---|
| `AppConf.xml` | 플래시 전 원본. `app_to_use=3` — **원복용** |
| `AppConf_arbiter.xml` | 위에서 `app_to_use`만 `8`(APP_CUSTOM). 22행 1줄만 다름 |
| `McConf.xml` | 모터 설정 원본. 중재기로 바꿔도 그대로 사용 |
| `after_*.xml` | 적용 후 읽어온 값 (검증용) |
| `60_mk6.bin` | 구운 이미지. md5 `d0632d7e3368a6e8f1491a780d4af9bc` |

CustomConf는 비어 있어서(`--getCustomConf`가 "Could not load config") 백업 대상 없음.

## 2026-10-30 작업 내역

1. `AppConf`/`McConf` 백업
2. `--uploadFirmware 60_mk6.bin` → `Firmware upload OK!`, FW 6.05 → **6.06**
3. `--setMcConf McConf.xml` + `--setAppConf AppConf_arbiter.xml` 재적용
4. 검증: `arb` 터미널 명령 응답 O, custom app data 50 Hz, AppConf 읽기 **완전 일치**,
   모터 기능값 전부 동일

6.05 → 6.06 마이그레이션으로 생긴 무해한 차이 (nuc4 보드와 동일 패턴):
`ConfigVersion 3→4`, 신규 필드 `foc_hfi_amb_*`/`foc_overmod_factor`,
`foc_offsets_cal_on_boot` → `foc_offsets_cal_mode`(값 1 유지),
`foc_offsets_*` 미세 변화(부팅마다 재측정되는 값), `motor_brand`/`motor_model` 공백화.

## 적용 / 원복

```bash
V=/home/leesh/Downloads/vesc_tool_free_linux/vesc_tool_6.06; P=/dev/ttyACM0
# 플래시 후에는 항상 둘 다 재적용 (굽는 순간 기본값으로 초기화된다)
$V --offscreen --vescPort $P --setMcConf  McConf.xml
$V --offscreen --vescPort $P --setAppConf AppConf_arbiter.xml
# 원복: 위 AppConf_arbiter.xml 대신 AppConf.xml
```

## ⚠ 조향 교정값은 이 차 값이 아니다

구운 이미지의 `STR_CENTER 0.6263 / SERVO_MIN 0.2526 / SERVO_MAX 1.0` 은
**nuc4 쪽 차에서 2026-10-23 눈으로 측정한 값**이다. 이 보드를 다른 차에 넣으면
그 차에서 직진 위치를 다시 재고 `firmware/app_arbiter.c` 를 고쳐 재빌드해야 한다.

## 남은 일

- 수신기를 COMM 헤더에 배선 (RX→PB11, TX→PB10, 5V, GND). 지금은 `rc_age`가
  계속 올라가고 `ACTUAL=ESTOP` 상태 — RC 링크가 없으니 정상이다.
- 배선 후 `arb` 로 `rc_age`가 0 근처인지 확인.
