# Prototypes (superseded)

Early LispBM implementations, written while working out how to get CRSF into a
VESC at all. **None of these are used** — the shipped implementation is
`firmware/app_arbiter.c`, in C.

They are kept because the comments record what was tried and what broke, which
is the part that is expensive to rediscover:

| File | What it was for |
|---|---|
| `lisp/uart_diag.lisp`, `lisp/uart_loopback.lisp` | proving the COMM UART worked at all |
| `lisp/rx_test.lisp`, `lisp/rc_read.lisp` | first receiver reads |
| `lisp/crsf_read.lisp` | CRSF frame decoding, and the note that the receiver's T pad goes to COMM RX (PB11) |
| `lisp/manual_mode.lisp` | sticks to motor and servo |
| `lisp/rc_failsafe.lisp` | failsafe logic; written against iBUS before the link was confirmed as ELRS/CRSF |
| `lisp/arbiter.lisp` | the full arbiter in Lisp — the direct ancestor of `app_arbiter.c` |

Notes worth salvaging if you ever write LispBM for a VESC:

- wrap a `let` body in `progn`
- use nested `if` rather than `cond`
- pass state through global `def` variables with `setq`
- a blocking `read` stalls everything; put reception on its own `spawn`ed thread

`rc_failsafe.lisp` still carries its own warning that its decoder is an iBUS
skeleton and needs replacing for CRSF. That replacement is what became the C
implementation.

Why the move to C: the arbiter has to be the single writer to the motor and
servo, and it has to block the computer's comm commands while it drives. That
gate lives in `comm/commands.c` and is not reachable from LispBM.
