// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef DEBUG_H
#define DEBUG_H

// Firmware debug log, emitted on the third USB-CDC port ("Debug Log").
// Output is dropped unless a host has that port open, so it is safe to call
// freely. Do NOT call from an interrupt handler.
void dbg_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif  // DEBUG_H
