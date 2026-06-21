// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdbool.h>

// Transparent USB-CDC (port 0) <-> hardware-UART bridge.
void bridge_init(void);
void bridge_task(void);

// GP0<->GP1 loopback continuity self-test. Returns true if RX follows TX.
// Non-destructive: restores the UART before returning.
bool bridge_selftest(void);

#endif  // BRIDGE_H
