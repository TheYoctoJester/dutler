// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Minimal host shim for <tusb.h> — command.c only calls tud_task() (in its
// bootsel countdown). NOT the real TinyUSB header.
#ifndef DUTLER_SHIM_TUSB_H
#define DUTLER_SHIM_TUSB_H

void tud_task(void);

#endif  // DUTLER_SHIM_TUSB_H
