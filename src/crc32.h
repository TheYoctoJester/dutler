// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

#ifndef DUTLER_CRC32_H
#define DUTLER_CRC32_H

#include <stddef.h>
#include <stdint.h>

// Standard reflected CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF) — the same
// "zlib/PNG" CRC. Pure; no SDK dependency.
uint32_t dutler_crc32(const void *data, size_t len);

#endif  // DUTLER_CRC32_H
