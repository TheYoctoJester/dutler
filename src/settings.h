// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define RELAY_NAME_MAX 16  // including the NUL terminator

// Persisted settings (relays always boot OFF, so their state is NOT stored).
//
// CHANGING THIS STRUCT? It is the on-flash payload. Add fields at the END only
// (append-only) and follow the versioning/migration steps documented at the top
// of settings.c — reordering or resizing existing fields breaks stored records.
typedef struct {
    uint32_t baud;                                 // bridge UART boot baud rate
    uint8_t data_bits;                             // 5..8
    uint8_t parity;                                // 0 = none, 1 = odd, 2 = even
    uint8_t stop_bits;                             // 1 or 2
    char relay_name[RELAY_COUNT][RELAY_NAME_MAX];  // "" = unnamed
    uint8_t reserved;  // explicit trailing pad: keeps the layout free of any
                       // implicit padding (enforced by static asserts in
                       // settings.c). Was the compiler's pad byte before;
                       // same offset, so stored records stay compatible.
} settings_t;

extern settings_t g_settings;

// Load from flash into g_settings (falls back to defaults if absent/invalid).
void settings_load(void);

// Persist g_settings to flash. Returns true on success (verified read-back).
bool settings_save(void);

// Erase the stored record and reset g_settings to defaults (factory reset).
void settings_reset(void);

#endif  // SETTINGS_H
