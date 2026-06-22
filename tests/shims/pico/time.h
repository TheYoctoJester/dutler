// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Minimal host shim for <pico/time.h>. The fake make/time_reached pair makes any
// "wait until deadline" loop fall through immediately in tests.
#ifndef DUTLER_SHIM_PICO_TIME_H
#define DUTLER_SHIM_PICO_TIME_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t absolute_time_t;

absolute_time_t make_timeout_time_ms(uint32_t ms);
bool time_reached(absolute_time_t t);

#endif  // DUTLER_SHIM_PICO_TIME_H
