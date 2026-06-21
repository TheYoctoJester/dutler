// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef RELAY_H
#define RELAY_H

// Output control command channel on USB-CDC port 1 (newline-terminated text).
void relay_init(void);
void relay_task(void);

#endif  // RELAY_H
