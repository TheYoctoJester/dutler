// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define OUT_NAME_MAX 16  // including the NUL terminator

// Persisted settings (outputs always boot OFF, so their state is NOT stored).
//
// CHANGING THIS STRUCT? It is the on-flash payload. Add fields at the END only
// (append-only) and follow the versioning/migration steps documented at the top
// of settings_codec.c — reordering or resizing existing fields breaks stored records.
typedef struct {
    uint32_t baud;                           // bridge UART boot baud rate
    uint8_t data_bits;                       // 5..8
    uint8_t parity;                          // 0 = none, 1 = odd, 2 = even
    uint8_t stop_bits;                       // 1 or 2
    char out_name[OUT_COUNT][OUT_NAME_MAX];  // "" = unnamed
    // control-port local echo: 0 = off, 1 = on. Lives in the byte that was explicit trailing
    // pad (still keeps the layout free of implicit padding — see the static asserts in
    // settings_codec.c). Same offset/size as the old pad byte, so stored records stay
    // compatible and read back echo = 0 (off), matching prior behaviour.
    uint8_t echo;
} settings_t;

extern settings_t g_settings;

// Load from flash into g_settings (falls back to defaults if absent/invalid).
void settings_load(void);

// Persist g_settings to flash. Returns true on success (verified read-back).
bool settings_save(void);

// Erase the stored record and reset g_settings to defaults (factory reset).
void settings_reset(void);

// Human-readable UART line mode. Shared so the control port's `status` and the
// bridge's line-coding log render it identically.
//   printf args: (unsigned long)baud, (unsigned)data_bits, parity_char, (unsigned)stop_bits
#define UART_MODE_FMT "%lu baud %u%c%u"
// Map a parity code (0 = none, 1 = odd, 2 = even) to its 'N'/'O'/'E' letter.
char parity_to_char(uint8_t parity_code);

#endif  // SETTINGS_H
