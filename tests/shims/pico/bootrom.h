// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Minimal host shim for <pico/bootrom.h>. Deliberately NOT marked noreturn so
// the fake (fake_sdk.c) can record the request and let the test continue.
#ifndef DUTLER_SHIM_PICO_BOOTROM_H
#define DUTLER_SHIM_PICO_BOOTROM_H

#include <stdint.h>

void reset_usb_boot(uint32_t usb_activity_gpio_pin_mask, uint32_t disable_interface_mask);

#endif  // DUTLER_SHIM_PICO_BOOTROM_H
