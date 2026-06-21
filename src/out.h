// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

#ifndef OUT_H
#define OUT_H

// Output control command channel on USB-CDC port 1 (newline-terminated text).
void out_init(void);
void out_task(void);

#endif  // OUT_H
