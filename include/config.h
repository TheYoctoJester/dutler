// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
//  Build-time configuration for DUTler (RP2040 firmware).
//  All the knobs you are likely to change live here.
// =====================================================================

// Firmware version — normally injected by CMake (git describe). Fallback for
// builds that don't set it (e.g. the host unit tests).
#ifndef DUTLER_VERSION
#define DUTLER_VERSION "unknown"
#endif

// ---- USB identity ---------------------------------------------------
// Development / test identifiers only (pid.codes test VID/PID).
// Obtain your own VID/PID before distributing hardware.
#define USB_VID 0x1209  // pid.codes
#define USB_PID 0x0001  // test PID
#define USB_BCD 0x0200  // USB 2.0

// ---- CDC interface indices (which serial port is which) -------------
#define CDC_ITF_BRIDGE 0  // transparent USB <-> hardware-UART bridge
#define CDC_ITF_OUT 1     // text command channel for the control outputs
#define CDC_ITF_DEBUG 2   // firmware debug-log output (read-only stream)

// ---- UART bridge ----------------------------------------------------
#define BRIDGE_UART uart0
#define BRIDGE_UART_IRQ UART0_IRQ
#define BRIDGE_TX_PIN 0  // GP0 -> UART0 TX
#define BRIDGE_RX_PIN 1  // GP1 -> UART0 RX
#define BRIDGE_INIT_BAUD 115200
// Accepted range for `set baud`. Plain ints (no u suffix) so they can be
// stringified into the error message — see cmd_set().
#define BRIDGE_BAUD_MIN 50
#define BRIDGE_BAUD_MAX 4000000

// ---- Control outputs (MOSFET strap/reset drivers + power relay) -----
// Exposed as `out 1..OUT_COUNT` on the control port. On the v1 HAT these map to:
//   out 1 -> GP2 -> low-side MOSFET (Q1) -> J1
//   out 2 -> GP3 -> low-side MOSFET (Q2) -> J3
//   out 3 -> GP4 -> ULN2003 -> power relay (K1) -> J4
//   out 4 -> GP5 -> intentional spare: no driver on the v1 HAT, but kept as a
//            ready-to-use 4th output for bare-Pico wiring and future board revs.
// OUT_COUNT stays 4 on purpose: it sizes the persisted settings record
// (settings_v1_t in settings_codec.c), so shrinking it is a stored-format break
// that needs a SETTINGS_VERSION bump + migrator, not a one-line edit.
#define OUT_COUNT 4
// GPIO pins for outputs 1..OUT_COUNT. Avoid GP0/GP1 (bridge UART).
#define OUT_PINS {2, 3, 4, 5}
// Set to 1 if an output is active-low (driven LOW = asserted/energized).
#define OUT_ACTIVE_LOW 0

// ---- Activity LED ---------------------------------------------------
// Heartbeat LED, when the board exposes one on a plain GPIO (GP25 on a stock
// Pico). Wireless boards (Pico W / Pico 2 W) drive their LED via the CYW43 chip
// and define no PICO_DEFAULT_LED_PIN — there the heartbeat is simply skipped
// (DUTLER_HAVE_LED 0), avoiding a wireless-stack dependency.
#if defined(PICO_DEFAULT_LED_PIN)
#define DUTLER_HAVE_LED 1
#define LED_PIN PICO_DEFAULT_LED_PIN
#else
#define DUTLER_HAVE_LED 0
#endif

// ---- Reset behaviour ------------------------------------------------
// Opening the DEBUG port at 1200 baud reboots into the USB bootloader (the
// classic "1200-baud touch"). Set to 0 to disable; the explicit 'bootsel'
// command on the control port always works regardless.
#define ENABLE_BAUD_TOUCH_RESET 1

// ---- Watchdog & heartbeat -------------------------------------------
// Auto-reboot if the main loop stops feeding the watchdog for this long
// (e.g. a wedged USB stack). Must exceed the worst-case blocking section,
// which is the flash sector erase during a settings 'save' (hundreds of ms
// with interrupts masked); the save path feeds the watchdog just before it.
#define WATCHDOG_TIMEOUT_MS 2000
// Activity-LED blink period; it also bounds the main loop's idle sleep, so the
// watchdog is fed every HEARTBEAT_MS. MUST stay comfortably below
// WATCHDOG_TIMEOUT_MS or an idle bus would trip the watchdog.
#define HEARTBEAT_MS 500

// ---- Control-port line input / interactive shell --------------------
// Max command line length (including the NUL). Bounds both the plain line reader
// and the interactive editor (lineedit.c). Not named LINE_MAX: that clashes with
// a POSIX <limits.h> macro pulled in by the host unit tests.
#define CONSOLE_LINE_MAX 128
// Interactive-shell command history depth (RAM ring; see lineedit.c).
#define HISTORY_DEPTH 16
// Prompt printed in interactive-shell mode.
#define SHELL_PROMPT "dutler> "
// Assumed terminal width until an ESC[6n cursor-position report answers.
#define DEFAULT_TERM_COLS 80
// Single kill-buffer size for Ctrl-U/K/W + Ctrl-Y yank.
#define KILL_MAX CONSOLE_LINE_MAX

// ---- User key/value store (kvstore.c) -------------------------------
// Arbitrary user strings persisted under the control-port `set kv`/`get kv`
// commands, in their own flash A/B slots (separate from the built-in settings).
#define KV_KEY_MAX 32        // max key length, including NUL
#define KV_VALUE_MAX 112     // max value length, including NUL (bounded by CONSOLE_LINE_MAX)
#define KV_STORE_BYTES 2048  // total packed payload budget ("key\0value\0" repeated)

#endif  // CONFIG_H
