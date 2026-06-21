// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
//  Build-time configuration for the USB-UART + Relay adapter (RP2040)
//  All the knobs you are likely to change live here.
// =====================================================================

// ---- USB identity ---------------------------------------------------
// Development / test identifiers only (pid.codes test VID/PID).
// Obtain your own VID/PID before distributing hardware.
#define USB_VID 0x1209  // pid.codes
#define USB_PID 0x0001  // test PID
#define USB_BCD 0x0200  // USB 2.0

// ---- CDC interface indices (which serial port is which) -------------
#define CDC_ITF_BRIDGE 0  // transparent USB <-> hardware-UART bridge
#define CDC_ITF_RELAY  1  // text command channel for the relays
#define CDC_ITF_DEBUG  2  // firmware debug-log output (read-only stream)

// ---- UART bridge ----------------------------------------------------
#define BRIDGE_UART      uart0
#define BRIDGE_UART_IRQ  UART0_IRQ
#define BRIDGE_TX_PIN    0  // GP0 -> UART0 TX
#define BRIDGE_RX_PIN    1  // GP1 -> UART0 RX
#define BRIDGE_INIT_BAUD 115200

// ---- Relays ---------------------------------------------------------
#define RELAY_COUNT 4
// GPIO pins for relays 1..RELAY_COUNT. Avoid GP0/GP1 (bridge UART).
#define RELAY_PINS \
    { 2, 3, 4, 5 }
// Set to 1 if the relay board energizes on a LOW level (active-low input).
#define RELAY_ACTIVE_LOW 0

// ---- Activity LED ---------------------------------------------------
#define LED_PIN PICO_DEFAULT_LED_PIN  // GP25 on a stock Pico

// ---- Reset behaviour ------------------------------------------------
// Opening the DEBUG port at 1200 baud reboots into the USB bootloader (the
// classic "1200-baud touch"). Set to 0 to disable; the explicit 'bootsel'
// command on the relay port always works regardless.
#define ENABLE_BAUD_TOUCH_RESET 1

// ---- Watchdog -------------------------------------------------------
// Auto-reboot if the main loop stops feeding the watchdog for this long
// (e.g. a wedged USB stack). Must exceed the worst-case blocking section,
// which is the flash sector erase during a settings 'save' (hundreds of ms
// with interrupts masked); the save path feeds the watchdog just before it.
#define WATCHDOG_TIMEOUT_MS 2000

#endif  // CONFIG_H
