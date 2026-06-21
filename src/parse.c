// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

#include "parse.h"

#include <stdlib.h>

bool parse_u32(const char *s, uint32_t *out) {
    if (!s || s[0] < '0' || s[0] > '9') return false;  // must start with a digit
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*end != '\0') return false;  // reject anything not fully numeric
    *out = (uint32_t)v;
    return true;
}
