/*
 * Copyright 2026 Sanghun Lee
 *
 * This file is part of the VESC ELRS Arbiter, built on the VESC firmware
 * (Copyright Benjamin Vedder, benjamin@vedder.se).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * RC-failsafe arbiter (F1TENTH) — included via APP_CUSTOM_TO_USE.
 * ----------------------------------------------------------------------------
 * Reads an ELRS receiver (CRSF @ 420000, 8N1) directly on the COMM UART
 * (USART3, PB11 = RX) and arbitrates between the RC transmitter and the
 * computer (which sends standard motor/servo commands over USB comm).
 *
 * Both inputs are ALWAYS received. A single 3-position switch (SEL channel)
 * selects AUTO / ESTOP / MANUAL; the arbiter is the single writer to motor & servo.
 * A separate dial channel (MAXSPD) sets the RC max speed in m/s.
 *
 *   ch0 = steering   ch2 = throttle   ch5 = SEL (3-pos: low=AUTO mid=ESTOP high=MANUAL)
 *   ch6 = MAXSPD (dial -> max speed MAXV_MIN..MAXV_MAX m/s, RC driving only)
 *   ch7 = CTRL   (SD: RC throttle as CURRENT control or SPEED control)
 *
 * CURRENT: stick = motor current (THR_MAX cap), throttle cut only above maxv.
 * SPEED:   stick = target speed, full stick = maxv (VESC speed PID; tune it in
 *          VESC Tool -> Motor Settings -> PID Controllers). Boots in CURRENT.
 * The mode only changes while the throttle stick is centered, so flipping it
 * mid-drive cannot make the car lurch.
 *
 * Priority: RC lost > ESTOP(SEL mid) > MANUAL(SEL high, RC drives)
 *           > AUTO(SEL low, computer if fresh) > AUTO but computer stale -> STOP
 *
 * Deliberately NOT a fallback to RC while sel==AUTO: an accidental stick bump
 * while the switch is left on AUTO must not move the car. RC only ever drives
 * when the switch is explicitly on MANUAL. If the computer goes stale in
 * AUTO, the car just stops and waits (same as ESTOP) until either the
 * computer comes back or the operator flips the switch to MANUAL themselves.
 *
 * When the arbiter drives (manual or stop), app_arbiter_blocks_comm()
 * returns true and commands.c ignores computer motor/servo commands so the
 * two never fight. Requires "App to Use = Custom" and servo output enabled.
 *
 * Telemetry back to the radio: sent on the same UART's TX line (wire
 * receiver-R <-> VESC-TX) as standard CRSF frames the receiver relays:
 *   - Vario (0x07), every TELEM_PERIOD_MS: VSpd (0.01 m/s) = speed. Smallest
 *     CRSF frame (6 bytes) so it fits the most updates through ELRS.
 *   - Battery Sensor (0x08), every TELEM_PERIOD_MS in AUTO, else every
 *     SLOW_PERIOD_MS (in MANUAL/ESTOP the radio shows its own sticks/dial
 *     locally, so it does not need these fast), all fields signed:
 *       RxBt (0.1 V)  = speed m/s * 10
 *       Curr (0.1 A)  = throttle % * 10   (-100.0..100.0)
 *       Capa (mAh)    = max speed in cm/s
 *       Bat% (%)      = steer %            (-100..100, int8)
 *   - Flight Mode (0x21) "<AUTO|ESTOP|MANUAL> <CUR|SPD>", on change + every FM_PERIOD_MS
 * ELRS relays a limited number of telemetry frames per second (Telem Ratio),
 * so everything that changes fast lives in ONE frame.
 *
 * Note: on this HW the CURRENT_FILTER macro toggles PB11 via palClearPad,
 * which is a no-op while the pin is in UART alternate-function mode, so motor
 * operation does not disturb CRSF reception.
 */

#include "app.h"
#include "ch.h"
#include "hal.h"

#include "mc_interface.h"
#include "utils_math.h"
#include "hw.h"
#include "commands.h"
#include "terminal.h"
#include "timeout.h"
#include "buffer.h"
#include "driver/pwm_servo.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

// ---- CRSF ----
#define CRSF_SYNC        0xC8
#define CRSF_TYPE_RC     0x16
#define CRSF_BAUD        420000

// ---- channel map (0-based) ----
#define CH_STEER   0
#define CH_THR     2
#define CH_SEL     5    // 3-position switch: low=AUTO mid=ESTOP high=MANUAL
#define CH_MAXSPD  6    // dial -> max speed (m/s)
#define CH_CTRL    7    // SD: current <-> speed control
#define CTRL_SW_MOMENTARY 1 // 1: SD is a push button (toggle on press), 0: latching switch (high = SPEED)

// ---- tuning ----
// Steering is calibrated per car; these are measurements, not defaults, and
// they will not centre a different vehicle. STR_CENTER is where this car's
// wheels point straight -- it is NOT the servo midpoint -- so each side gets
// its own gain and full stick reaches that side's mechanical limit without
// ever pushing past it.
//
// Measured 2026-11-25 on the 60_MK6_HP board (UUID 3B 00 24 ...), servo output
// at the stock 1000..2000 us range:
//     centre 0.3040 = 1304 us
//     left   0.0160 = 1016 us   ->  288 us of travel
//     right  0.7000 = 1700 us   ->  396 us of travel
//
// Both limits are mechanical: the linkage binds there. The left limit sits only
// 16 us above the 1000 us output floor, so widening the output range buys
// essentially nothing on this car -- it was tried and reverted.
//
// The range is left ASYMMETRIC on purpose, because the firmware can express it
// (STR_RANGE_L / STR_RANGE_R) and clipping would throw away 108 us of right-hand
// steering. NOTE: a ROS stack steers with a single gain
// (steering_angle_to_servo_gain) and CANNOT express this, so if this car is ever
// driven autonomously, clip the right side to match the left (SERVO_MAX 0.5920)
// or manual and AUTO will steer differently. ROS also keeps its own copy of the
// centre in stack_master/config/CAR/vehicle_config.yaml
// (steering_angle_to_servo_offset); change both together.
#define STR_CENTER     0.3040f // servo position for straight ahead
#define SERVO_MIN      0.0160f // left travel limit, mechanical
#define SERVO_MAX      0.7000f // right travel limit, mechanical
#define STR_RANGE_L    (STR_CENTER - SERVO_MIN)  // 0.2880, stick to the left
#define STR_RANGE_R    (SERVO_MAX - STR_CENTER)  // 0.3960, stick to the right
#define THR_MAX        0.60f   // motor current-rel cap at full stick
#define MAXV_MIN       0.5f    // m/s, dial fully CCW
#define MAXV_MAX       8.0f    // m/s, dial fully CW
#define SPD_LIMIT_BAND 0.10f   // above max speed, throttle fades to 0 over this fraction of maxv
#define DEADBAND       0.05f
#define SEL_LOW_MAX    500     // CH_SEL below this -> low position (AUTO)
#define SEL_HIGH_MIN   1500    // CH_SEL above this -> high position (MANUAL); between = mid (ESTOP)
#define RC_TIMEOUT_MS  100     // RC frame loss
#define CMD_TIMEOUT_MS 500     // computer command loss (auto -> stop)
#define TELEM_PERIOD_MS 50     // vario frame (speed) to radio; battery frame too in AUTO
#define SLOW_PERIOD_MS 500     // battery frame period outside AUTO
#define FM_PERIOD_MS   1000    // flight-mode text refresh (also sent on change)
#define SPEED_TO_ERPM_GAIN 4614.0f // ERPM per m/s -- same as vesc_driver speed_to_erpm_gain

// ---- gate state shared with commands.c ----
static volatile bool arbiter_driving = false;
static volatile systime_t last_comm_cmd = 0;
static volatile uint32_t comm_cmd_count = 0;   // debug: how many comm motor/servo cmds seen

bool app_arbiter_blocks_comm(void) {
	return arbiter_driving;
}

void app_arbiter_report_comm_cmd(void) {
	last_comm_cmd = chVTGetSystemTimeX();
	comm_cmd_count++;
}

// ---- telemetry back to PC (so it sees servo/throttle/mode in every mode) ----
// mode: 0=disarm/stop (also AUTO-but-stale), 1=manual(RC), 2=auto(computer)
static volatile int arb_mode = 0;
static volatile float g_servo = STR_CENTER; // last applied servo (arbiter or comm passthrough)

// Called by commands.c when an auto (passthrough) servo command is applied,
// so the reported servo matches reality in auto mode too.
void app_arbiter_note_servo(float pos) {
	g_servo = pos;
}

// ---- private ----
static THD_FUNCTION(arbiter_thread, arg);
static THD_WORKING_AREA(arbiter_thread_wa, 2048);
static volatile bool stop_now = true;
static volatile bool is_running = false;

static uint16_t rc_ch[16];
static systime_t last_rc_frame = 0;
static uint8_t rxbuf[64];
static int rxlen = 0;

static uint16_t crsf_unpack(const uint8_t *pl, int i) {
	int bo = (i * 11) / 8;
	int sh = (i * 11) % 8;
	uint32_t v = (uint32_t)pl[bo] >> sh;
	v |= (uint32_t)pl[bo + 1] << (8 - sh);
	v |= (uint32_t)pl[bo + 2] << (16 - sh);
	return (uint16_t)(v & 0x7FF);
}

static void crsf_parse(void) {
	while (rxlen >= 4) {
		if (rxbuf[0] != CRSF_SYNC) {
			memmove(rxbuf, rxbuf + 1, --rxlen);
			continue;
		}
		int flen = rxbuf[1];              // type + payload + crc
		if (flen < 2 || flen > 62) {
			memmove(rxbuf, rxbuf + 1, --rxlen);
			continue;
		}
		int total = 2 + flen;
		if (rxlen < total) {
			break;                        // wait for the rest of the frame
		}
		if (rxbuf[2] == CRSF_TYPE_RC) {   // RC channels packed (22B payload)
			for (int i = 0; i < 16; i++) {
				rc_ch[i] = crsf_unpack(&rxbuf[3], i);
			}
			last_rc_frame = chVTGetSystemTimeX();
		}
		memmove(rxbuf, rxbuf + total, rxlen - total);
		rxlen -= total;
	}
}

static float rc_norm(uint16_t raw) {      // 172..1811 (center 992) -> -1..1
	float x = ((float)raw - 992.0f) / 819.0f;
	if (x > 1.0f) x = 1.0f;
	if (x < -1.0f) x = -1.0f;
	return x;
}
static float deadband(float x) {
	return (fabsf(x) < DEADBAND) ? 0.0f : x;
}

// 3-position SEL switch: low=AUTO, mid=ESTOP, high=MANUAL
typedef enum { SEL_AUTO = 0, SEL_ESTOP = 1, SEL_MANUAL = 2 } sel_state_t;
static sel_state_t read_sel(void) {
	uint16_t v = rc_ch[CH_SEL];
	if (v < SEL_LOW_MAX)  return SEL_AUTO;
	if (v > SEL_HIGH_MIN) return SEL_MANUAL;
	return SEL_ESTOP;
}

static float get_speed_ms(void) {
	return mc_interface_get_rpm() / SPEED_TO_ERPM_GAIN;
}

// MAXSPD dial (raw 172..1811) -> MAXV_MIN..MAXV_MAX m/s
static float maxv_dyn = MAXV_MIN;
static float read_maxv(void) {
	float d = ((float)rc_ch[CH_MAXSPD] - 172.0f) / (1811.0f - 172.0f);
	if (d < 0.0f) d = 0.0f;
	if (d > 1.0f) d = 1.0f;
	return MAXV_MIN + d * (MAXV_MAX - MAXV_MIN);
}

static float g_thr = 0.0f;   // last applied throttle, -1..1 (fraction of THR_MAX)

// RC throttle control mode (SD)
static volatile bool ctrl_speed = false;
static bool ctrl_btn_prev = false;

static void update_ctrl_mode(void) {
	bool high = rc_ch[CH_CTRL] > SEL_HIGH_MIN;
	bool stick_centered = deadband(rc_norm(rc_ch[CH_THR])) == 0.0f;
#if CTRL_SW_MOMENTARY
	if (high && !ctrl_btn_prev && stick_centered) {
		ctrl_speed = !ctrl_speed;
	}
	ctrl_btn_prev = high;
#else
	(void)ctrl_btn_prev;
	if (stick_centered) {
		ctrl_speed = high;
	}
#endif
}

static void drive_rc(void) {
	float st = rc_norm(rc_ch[CH_STEER]);
	float th = deadband(rc_norm(rc_ch[CH_THR]));
	float v = get_speed_ms();
	float servo = STR_CENTER + (st < 0.0f ? STR_RANGE_L : STR_RANGE_R) * st;

	if (ctrl_speed) {
		// SPEED: stick -> target speed, full stick = maxv
		pwm_servo_set_servo_out(servo);
		mc_interface_set_pid_speed(th * maxv_dyn * SPEED_TO_ERPM_GAIN);
		g_servo = servo;
		g_thr = th;
		timeout_reset();
		return;
	}

	// Pure current control below maxv. Only once |v| exceeds maxv (while
	// pushing in the direction of travel) the throttle fades to 0 over
	// SPD_LIMIT_BAND*maxv. Braking/reversing against the motion is never limited.
	if (th * v > 0.0f && fabsf(v) > maxv_dyn) {
		float band = SPD_LIMIT_BAND * maxv_dyn;
		float scale = 1.0f - (fabsf(v) - maxv_dyn) / band;
		if (scale < 0.0f) scale = 0.0f;
		if (scale > 1.0f) scale = 1.0f;
		th *= scale;
	}

	pwm_servo_set_servo_out(servo);
	mc_interface_set_current_rel(THR_MAX * th);
	g_servo = servo;
	g_thr = th;
	timeout_reset();
}
static void stop_out(void) {
	mc_interface_set_current_rel(0.0f);
	pwm_servo_set_servo_out(STR_CENTER);
	g_servo = STR_CENTER;
	g_thr = 0.0f;
	timeout_reset();
}

// Send [0xA5, mode, servo*1000, rc_steer*1000, rc_throttle*1000] to the PC.
static void send_feedback(void) {
	uint8_t buf[8];
	int32_t ind = 0;
	buf[ind++] = 0xA5;                 // marker for the RC-arbiter feedback packet
	buf[ind++] = (uint8_t)arb_mode;
	buffer_append_int16(buf, (int16_t)(g_servo * 1000.0f), &ind);
	buffer_append_int16(buf, (int16_t)(rc_norm(rc_ch[CH_STEER]) * 1000.0f), &ind);
	buffer_append_int16(buf, (int16_t)(deadband(rc_norm(rc_ch[CH_THR])) * 1000.0f), &ind);
	commands_send_app_data(buf, ind);
}

// ---- CRSF telemetry back to the radio (via receiver, over the same UART TX) ----
// Standard frame types, decoded natively by EdgeTX as RxBt/Curr/Capa/Fuel/FM
// sensors -- no Lua script needed on the radio side.
#define CRSF_FRAMETYPE_VARIO          0x07
#define CRSF_FRAMETYPE_BATTERY_SENSOR 0x08
#define CRSF_FRAMETYPE_FLIGHT_MODE    0x21

static uint8_t crsf_crc8(const uint8_t *ptr, int len) {
	uint8_t crc = 0;
	for (int i = 0; i < len; i++) {
		crc ^= ptr[i];
		for (int j = 0; j < 8; j++) {
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
		}
	}
	return crc;
}

// buf must be large enough: 2 (addr,len) + type + payload + crc
static int crsf_build_frame(uint8_t *buf, uint8_t type, const uint8_t *payload, int paylen) {
	buf[0] = CRSF_SYNC;
	buf[1] = (uint8_t)(paylen + 2);      // type + payload + crc
	buf[2] = type;
	memcpy(&buf[3], payload, paylen);
	buf[3 + paylen] = crsf_crc8(&buf[2], paylen + 1); // crc over type+payload
	return 4 + paylen;
}

static const char *mode_name(void) {
	switch (arb_mode) {
	case 1: return "MANUAL";
	case 2: return "AUTO";
	default: return "ESTOP";
	}
}

// EdgeTX ignores a field whose bytes are all 0xFF, i.e. exactly -1.
static int32_t avoid_minus_one(int32_t v) {
	return (v == -1) ? 0 : v;
}

static int last_fm_mode = -1;
static systime_t last_fm_sent = 0;
static systime_t last_batt_sent = 0;

static void send_crsf_telemetry(void) {
	uint8_t frame[32];
	int len;

	// Flight Mode text: only on change or every FM_PERIOD_MS, to leave the
	// limited ELRS telemetry slots for the fast-changing battery frame.
	int fm_key = arb_mode * 2 + (ctrl_speed ? 1 : 0);
	if (fm_key != last_fm_mode ||
			ST2MS(chVTTimeElapsedSinceX(last_fm_sent)) >= FM_PERIOD_MS) {
		char text[16];
		int n = snprintf(text, sizeof(text), "%s %s", mode_name(), ctrl_speed ? "SPD" : "CUR");
		if (n < 0) n = 0;
		if (n > (int)sizeof(text) - 1) n = sizeof(text) - 1;
		len = crsf_build_frame(frame, CRSF_FRAMETYPE_FLIGHT_MODE, (uint8_t *)text, n + 1);
		sdWrite(&HW_UART_DEV, frame, len);
		last_fm_mode = fm_key;
		last_fm_sent = chVTGetSystemTimeX();
	}

	// Vario: speed in cm/s, every call.
	{
		int32_t spd_cms = avoid_minus_one((int32_t)(get_speed_ms() * 100.0f));
		if (spd_cms > 32767) spd_cms = 32767;
		if (spd_cms < -32768) spd_cms = -32768;
		uint8_t payload[2];
		int32_t ind = 0;
		buffer_append_int16(payload, (int16_t)spd_cms, &ind);
		len = crsf_build_frame(frame, CRSF_FRAMETYPE_VARIO, payload, ind);
		sdWrite(&HW_UART_DEV, frame, len);
	}

	// Battery frame, repurposed (see header). All fields signed big-endian.
	if (arb_mode != 2 &&
			ST2MS(chVTTimeElapsedSinceX(last_batt_sent)) < SLOW_PERIOD_MS) {
		return;
	}
	last_batt_sent = chVTGetSystemTimeX();

	float thr_pct;
	if (arb_mode == 2) {
		thr_pct = mc_interface_get_duty_cycle_now() * 100.0f;  // AUTO: actual motor output
	} else {
		thr_pct = g_thr * 100.0f;                             // MANUAL/ESTOP: applied RC throttle
	}
	float steer_dev = g_servo - STR_CENTER;
	float steer_pct = steer_dev / (steer_dev < 0.0f ? STR_RANGE_L : STR_RANGE_R) * 100.0f;
	if (steer_pct > 100.0f) steer_pct = 100.0f;
	if (steer_pct < -100.0f) steer_pct = -100.0f;

	int32_t spd   = avoid_minus_one((int32_t)(get_speed_ms() * 10.0f));
	int32_t thr   = avoid_minus_one((int32_t)(thr_pct * 10.0f));
	int32_t maxv  = (int32_t)(maxv_dyn * 100.0f);
	int32_t steer = avoid_minus_one((int32_t)steer_pct);

	uint8_t payload[8];
	int32_t ind = 0;
	buffer_append_int16(payload, (int16_t)spd, &ind);
	buffer_append_int16(payload, (int16_t)thr, &ind);
	payload[ind++] = (uint8_t)((maxv >> 16) & 0xFF);
	payload[ind++] = (uint8_t)((maxv >> 8) & 0xFF);
	payload[ind++] = (uint8_t)(maxv & 0xFF);
	payload[ind++] = (uint8_t)(int8_t)steer;

	len = crsf_build_frame(frame, CRSF_FRAMETYPE_BATTERY_SENSOR, payload, ind);
	sdWrite(&HW_UART_DEV, frame, len);
}

// VESC Tool terminal command: toggle periodic (1 s) arbiter state print.
static volatile bool dbg_on = false;
static int dbg_tick = 0;
static int fb_tick = 0;
static int telem_tick = 0;

static const char *sel_name(sel_state_t s) {
	switch (s) {
	case SEL_AUTO:   return "AUTO";
	case SEL_MANUAL: return "MANUAL";
	default:         return "ESTOP";
	}
}

static void arb_print_state(void) {
	int age = ST2MS(chVTTimeElapsedSinceX(last_rc_frame));
	int cmd_age = ST2MS(chVTTimeElapsedSinceX(last_comm_cmd));
	// "sel" = physical switch position. "ACTUAL" = who is really driving right
	// now (e.g. sel=AUTO but computer dead -> ACTUAL=ESTOP, car just stops).
	commands_printf("steer %d thr %d sel %d(%s) maxv %d(%.2fm/s) v %.2f ctrl %d(%s) ACTUAL=%s | rc_age %d cmd_age %d cmds %lu driving %d",
			rc_ch[CH_STEER], rc_ch[CH_THR], rc_ch[CH_SEL], sel_name(read_sel()),
			rc_ch[CH_MAXSPD], (double)maxv_dyn, (double)get_speed_ms(), rc_ch[CH_CTRL], ctrl_speed ? "SPD" : "CUR", mode_name(),
			age, cmd_age, (unsigned long)comm_cmd_count, arbiter_driving);
}

static void terminal_arb(int argc, const char **argv) {
	(void)argc; (void)argv;
	dbg_on = !dbg_on;
	commands_printf("arb periodic print %s", dbg_on ? "ON (type arb again to stop)" : "OFF");
	arb_print_state();
}

void app_custom_start(void) {
	stop_now = false;

	terminal_register_command_callback(
			"arb", "Print RC arbiter state", 0, terminal_arb);

	// Take over the COMM UART for CRSF RX+TX (USART3, PB11=RX / PB10=TX).
	// TX carries telemetry back to the receiver -> radio; wire receiver R <-> VESC TX.
	static SerialConfig uart_cfg = { CRSF_BAUD, 0, 0, 0 }; // 8N1
	sdStop(&HW_UART_DEV);
	sdStart(&HW_UART_DEV, &uart_cfg);
	palSetPadMode(HW_UART_RX_PORT, HW_UART_RX_PIN,
			PAL_MODE_ALTERNATE(HW_UART_GPIO_AF) |
			PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUDR_PULLUP);
	palSetPadMode(HW_UART_TX_PORT, HW_UART_TX_PIN,
			PAL_MODE_ALTERNATE(HW_UART_GPIO_AF) |
			PAL_STM32_OSPEED_HIGHEST);

	chThdCreateStatic(arbiter_thread_wa, sizeof(arbiter_thread_wa),
			NORMALPRIO, arbiter_thread, NULL);
}

void app_custom_stop(void) {
	terminal_unregister_callback(terminal_arb);
	stop_now = true;
	while (is_running) {
		chThdSleepMilliseconds(1);
	}
}

void app_custom_configure(app_configuration *conf) {
	(void)conf;
}

static THD_FUNCTION(arbiter_thread, arg) {
	(void)arg;
	chRegSetThreadName("RC Arbiter");
	is_running = true;

	// center channels so a first cycle before any frame is safe
	for (int i = 0; i < 16; i++) {
		rc_ch[i] = 992;
	}

	for (;;) {
		if (stop_now) {
			is_running = false;
			return;
		}

		// 1) drain UART (non-blocking) and parse CRSF
		msg_t r;
		while ((r = sdGetTimeout(&HW_UART_DEV, TIME_IMMEDIATE)) != MSG_TIMEOUT) {
			if (rxlen < (int)sizeof(rxbuf)) {
				rxbuf[rxlen++] = (uint8_t)r;
			} else {
				memmove(rxbuf, rxbuf + 1, sizeof(rxbuf) - 1);
				rxbuf[sizeof(rxbuf) - 1] = (uint8_t)r;
			}
		}
		crsf_parse();

		// 2) freshness + inputs
		bool rc_alive = ST2MS(chVTTimeElapsedSinceX(last_rc_frame)) < RC_TIMEOUT_MS;
		bool cmd_fresh = ST2MS(chVTTimeElapsedSinceX(last_comm_cmd)) < CMD_TIMEOUT_MS;
		sel_state_t sel = read_sel();
		maxv_dyn = read_maxv();
		if (rc_alive) {
			update_ctrl_mode();
		}

		// 3) arbitrate (single writer)
		// NOTE: no RC fallback while sel==AUTO. If the computer goes stale in
		// AUTO, the arbiter stops the car instead of handing control to whatever
		// the stick happens to be at -- avoids an accidental bump "bolting" the
		// car. RC only ever drives when the switch is explicitly on MANUAL.
		if (!rc_alive) {
			arbiter_driving = true;         // RC lost -> hold & stop
			arb_mode = 0;
			stop_out();
		} else if (sel == SEL_ESTOP) {
			arbiter_driving = true;         // mid position = estop
			arb_mode = 0;
			stop_out();
		} else if (sel == SEL_MANUAL) {
			arbiter_driving = true;         // high position: RC drives
			arb_mode = 1;
			drive_rc();
		} else if (!cmd_fresh) {
			arbiter_driving = true;         // AUTO but computer dead -> STOP (no RC bleed-through)
			arb_mode = 0;
			stop_out();
		} else {
			arbiter_driving = false;        // AUTO: computer drives via comm (stay out)
			arb_mode = 2;
		}

		// 4) telemetry back to PC at 50 Hz (every 4th 5 ms cycle)
		if (++fb_tick >= 4) {
			fb_tick = 0;
			send_feedback();
		}

		// 4b) CRSF telemetry back to the radio (10 Hz)
		if (++telem_tick >= (TELEM_PERIOD_MS / 5)) {
			telem_tick = 0;
			send_crsf_telemetry();
		}

		// periodic debug print (toggled by the "arb" terminal command)
		if (dbg_on) {
			if (++dbg_tick >= 200) {        // 200 * 5 ms = 1 s
				dbg_tick = 0;
				arb_print_state();
			}
		} else {
			dbg_tick = 0;
		}

		chThdSleepMilliseconds(5);          // 200 Hz
	}
}
