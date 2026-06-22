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

// ---- Control outputs (power relay + MOSFET strap/reset drivers) -----
// Exposed as `out 1..OUT_COUNT` on the control port.
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

// ---- Watchdog -------------------------------------------------------
// Auto-reboot if the main loop stops feeding the watchdog for this long
// (e.g. a wedged USB stack). Must exceed the worst-case blocking section,
// which is the flash sector erase during a settings 'save' (hundreds of ms
// with interrupts masked); the save path feeds the watchdog just before it.
#define WATCHDOG_TIMEOUT_MS 2000

#endif  // CONFIG_H
