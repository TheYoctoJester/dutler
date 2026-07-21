// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Minimal host shim for <hardware/watchdog.h>. Deliberately NOT marked noreturn so
// the fake (fake_sdk.c) can record the request and let the test continue.
#ifndef DUTLER_SHIM_HARDWARE_WATCHDOG_H
#define DUTLER_SHIM_HARDWARE_WATCHDOG_H

#include <stdint.h>

void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);

#endif  // DUTLER_SHIM_HARDWARE_WATCHDOG_H
