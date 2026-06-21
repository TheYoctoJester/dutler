// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef DUTLER_PARSE_H
#define DUTLER_PARSE_H

#include <stdbool.h>
#include <stdint.h>

// Parse a base-10 unsigned integer. Rejects empty input, a leading sign or
// space, and trailing junk (so "", "12x", "1.5", "+5", "-1", " 5" all fail).
// Pure; no SDK dependency.
bool parse_u32(const char *s, uint32_t *out);

#endif  // DUTLER_PARSE_H
