// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef OUTPUTS_H
#define OUTPUTS_H

#include <stdbool.h>

// GPIO output channel: a power relay (output 1) plus low-side MOSFET drivers
// (outputs 2..) for a DUT's strap/boot-mode and reset lines. Outputs are
// 0-indexed here; the control protocol presents them 1-based.
void outputs_init(void);               // init all pins as outputs, driven OFF (safe state)
void outputs_set(int idx, bool on);    // drive output idx (honors OUT_ACTIVE_LOW)
bool outputs_get(int idx);             // current logical state of output idx
int outputs_resolve(const char *tok);  // a 1-based number or a configured name -> idx, or -1

#endif  // OUTPUTS_H
