// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "outputs.h"

#include <stdint.h>
#include <string.h>

#include "config.h"
#include "parse.h"
#include "pico/stdlib.h"
#include "settings.h"

static const uint8_t out_pins[OUT_COUNT] = OUT_PINS;
static bool out_state[OUT_COUNT];

void outputs_set(int idx, bool on) {
    out_state[idx] = on;
    bool level = OUT_ACTIVE_LOW ? !on : on;
    gpio_put(out_pins[idx], level);
}

bool outputs_get(int idx) { return out_state[idx]; }

void outputs_init(void) {
    for (uint8_t i = 0; i < OUT_COUNT; i++) {
        gpio_init(out_pins[i]);
        gpio_set_dir(out_pins[i], GPIO_OUT);
        outputs_set(i, false);  // always OFF at power-on (safe state)
    }
}

// Resolve an output reference: a 1-based number or a configured name.
int outputs_resolve(const char *tok) {
    if (!tok || !tok[0]) return -1;
    uint32_t n;
    if (parse_u32(tok, &n)) return (n >= 1 && n <= OUT_COUNT) ? (int)(n - 1) : -1;
    for (int i = 0; i < OUT_COUNT; i++)
        if (g_settings.out_name[i][0] && strcmp(g_settings.out_name[i], tok) == 0) return i;
    return -1;
}
