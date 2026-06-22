// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Test doubles for DUTler's hardware/transport seams. Each fake implements one
// production interface (flash_port, console, bridge, debug, the SDK shims) in
// RAM and exposes a few inspection/control helpers, declared here.
#ifndef DUTLER_TEST_FAKES_H
#define DUTLER_TEST_FAKES_H

#include <stdbool.h>
#include <stdint.h>

// --- flash_port_fake.c (RAM-backed flash) ---
void flash_fake_reset(void);                                          // all 0xFF, clears fail flag
void flash_fake_poke(uint32_t off, const uint8_t *data, uint32_t n);  // plant/corrupt bytes
void flash_fake_fail_next_program(void);  // next write erases but skips the program (power loss)

// --- fake_console.c (captures control-port output) ---
const char *fake_console_text(void);
void fake_console_clear(void);

// --- fake_bridge.c ---
void fake_bridge_set_selftest(bool ok);

// --- fake_sdk.c (bootrom) ---
bool fake_bootsel_requested(void);
void fake_bootsel_clear(void);

#endif  // DUTLER_TEST_FAKES_H
