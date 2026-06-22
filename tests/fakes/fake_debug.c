// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// The debug log is irrelevant to command/settings behavior; swallow it.
#include "util/debug.h"

void dbg_printf(const char *fmt, ...) { (void)fmt; }
