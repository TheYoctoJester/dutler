// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Minimal host shim for <pico/stdlib.h> — just enough of the GPIO API for
// outputs.c to compile and link against fake_sdk.c in the unit tests. NOT the
// real Pico SDK header.
#ifndef DUTLER_SHIM_PICO_STDLIB_H
#define DUTLER_SHIM_PICO_STDLIB_H

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

#define GPIO_OUT true

void gpio_init(uint gpio);
void gpio_set_dir(uint gpio, bool out);
void gpio_put(uint gpio, bool value);

#endif  // DUTLER_SHIM_PICO_STDLIB_H
