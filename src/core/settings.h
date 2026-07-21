// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define OUT_NAME_MAX 16  // including the NUL terminator

// Device name: a user/DUT-oriented label for this DUTler, surfaced in the USB
// product string (see usb_descriptors.c) so it shows up in /dev/serial/by-id.
// 24 keeps settings_t a multiple of 4 (no implicit padding — see the static
// asserts in settings_codec.c) and keeps "DUTler-"+name within the 31-char USB
// string-descriptor limit.
#define DEVICE_NAME_MAX 24  // including the NUL terminator

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
    // Device/DUT label, "" = unset. Appended (v3) after echo. Surfaced in the USB
    // product string; validated (charset/length) by the control port's `set name`.
    char device_name[DEVICE_NAME_MAX];
    // Control-port interactive shell: 0 = off (default; clean line protocol for
    // scripts), 1 = on (prompt, in-line editing, history — see lineedit.c). Appended
    // (v4) after device_name. reserved[3] keeps settings_t a multiple of 4 (no
    // implicit padding — see the static asserts in settings_codec.c) and leaves
    // room for future flag bytes without another version bump.
    uint8_t shell;
    uint8_t reserved[3];
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
