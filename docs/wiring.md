# Wiring and radio setup

## Receiver to VESC

The arbiter reads CRSF on the VESC's **COMM UART**, which on `hw_60_core.h`
(VESC 6 MK3-MK6) is USART3:

| Signal | VESC pin | Source |
|---|---|---|
| RX (into the VESC) | **PB11** | receiver TX |
| TX (out of the VESC) | **PB10** | receiver RX — needed for telemetry |
| 5 V | COMM header 5 V | receiver VCC |
| GND | COMM header GND | receiver GND |

Telemetry travels back out on the same UART's TX line, so **both** data wires
must be connected. With only RX wired the car drives fine and the handset shows
nothing.

> **Check the physical pin order against your board's silkscreen or
> schematic before you plug anything in.** This document gives signal names,
> not connector positions, and VESC 6 variants are not all identical. Getting
> 5 V and GND backwards will destroy the receiver.

Two notes on things that look like conflicts but are not:

- **`permanent_uart_enabled` can stay on.** It applies to `UART_PORT_BUILTIN`
  (SD4/SD5 on PC11/PC12, the nRF header), not the COMM header the arbiter uses.
- **The `CURRENT_FILTER` macro toggles PB11** via `palClearPad`, which is a
  no-op while the pin is in UART alternate-function mode. Motor operation does
  not disturb CRSF reception.

The steering servo goes on the usual servo output (ICU/TIM4 on PB6). Set
`servo_out_enable = 1` in the app config.

## ExpressLRS

- Receiver protocol: **CRSF, 420000 baud, 8N1** — the ELRS default for a
  full-duplex UART. Nothing to change on the receiver beyond binding it.
- **Telem Ratio** decides how much of the arbiter's telemetry actually reaches
  the handset. The arbiter offers frames every 50 ms; a stingy ratio simply
  drops most of them and the on-screen numbers go stale. 1:4 or better is
  comfortable.
- Packet rate only needs to beat `RC_TIMEOUT_MS` (100 ms) with margin. Any
  normal rate (50 Hz and up) does.

## EdgeTX model setup

Five channels, with the firmware's 0-based CRSF numbering on the left and
EdgeTX's 1-based mixer numbering on the right:

| Purpose | CRSF ch | EdgeTX ch | Suggested source | Notes |
|---|---|---|---|---|
| Steering | 0 | **CH1** | Ail | full travel both ways |
| Throttle | 2 | **CH3** | Thr | centre = stop, so no throttle-cut trim |
| Mode select | 5 | **CH6** | **SA** (3-pos) | low AUTO / mid ESTOP / high MANUAL |
| Max speed | 6 | **CH7** | P2 or S1 dial | CCW `MAXV_MIN` .. CW `MAXV_MAX` |
| Control mode | 7 | **CH8** | SD | current vs. speed control |

`vesc.lua` reads `sa`, `ch1`, `ch3` and `ch7` from the handset directly, so
**those sources must be on those channels** or the display will disagree with
the car. The script reads the switch by name (`sa`), so if you move the mode
switch to a different physical switch, change `num("sa")` in `vesc.lua` too.

### Which way is the 3-position switch?

The firmware decides by raw CRSF value:

```
CH_SEL <  500   -> low  = AUTO
500 .. 1500     -> mid  = ESTOP
CH_SEL > 1500   -> high = MANUAL
```

If your handset sends the switch inverted, AUTO and MANUAL will swap. Fix it in
the EdgeTX mixer (invert the weight) rather than in the firmware, so the Lua
script's local reading of `sa` stays consistent with what the car receives.

### SD: push button or latching?

`CTRL_SW_MOMENTARY` in `app_arbiter.c` must match your hardware:

- `1` — SD is a momentary push button; each press toggles current/speed control
- `0` — SD is a latching switch; high = speed control

A latching switch with `CTRL_SW_MOMENTARY 1` reads as a long press and the
control mode will appear to change on its own.

## Verifying it works

With the VESC on USB and the radio on, the `arb` terminal command prints live
state:

```
steer 1500 thr 992 sel 992(ESTOP) maxv 1200(4.25m/s) v 0.00 ctrl 992(CUR)
ACTUAL=ESTOP | rc_age 3 cmd_age 31359 cmds 0 driving 1
```

`rc_age` near 0 means the RC link is up. If it climbs without bound the
receiver is not reaching PB11 — wrong pin, wrong baud, or TX/RX swapped.

`arb` is a **toggle**: it leaves periodic printing on, so type it again to stop.

VESC Tool's CLI has no flag for terminal commands. Either use the GUI's
terminal, or send a `COMM_TERMINAL_CMD` (id 20) packet yourself and read the
`COMM_PRINT` (id 21) replies.
