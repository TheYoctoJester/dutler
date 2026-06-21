// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "debug.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "tusb.h"

void dbg_printf(const char *fmt, ...) {
    // Only emit when a host actually has the debug port open (DTR asserted).
    // Avoids filling the TX FIFO with stale logs nobody is reading.
    if (!tud_cdc_n_connected(CDC_ITF_DEBUG)) return;

    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;  // vsnprintf truncated; drop the NUL

    tud_cdc_n_write(CDC_ITF_DEBUG, buf, (uint32_t)n);
    tud_cdc_n_write_flush(CDC_ITF_DEBUG);
}
