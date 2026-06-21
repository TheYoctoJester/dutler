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

#endif  // CONFIG_H
