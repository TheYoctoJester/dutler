// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef DUTLER_SETTINGS_CODEC_H
#define DUTLER_SETTINGS_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings.h"  // settings_t, OUT_COUNT, OUT_NAME_MAX

// Pure (de)serialization of a settings record — no flash / SDK dependency, so it
// is unit-testable on the host. settings.c owns the flash I/O and slot logic.
//
//   +--------+---------+--------+----------------------------+-------+
//   | magic  | version |  seq   |   payload (settings_t)     |  crc  |
//   | u32 @0 | u32 @4  | u32 @8 |   @12 (v2) / @8 (v1)       | u32   |
//   +--------+---------+--------+----------------------------+-------+
//
// magic/version offsets are frozen forever so any build can read the version
// first and pick the right layout. crc is CRC-32 over [magic .. end of payload].

#define SETTINGS_MAGIC 0x52454C31u  // "REL1" — frozen
#define SETTINGS_VERSION 2u         // 2 = A/B with seq; 1 = legacy single slot

#define SC_OFF_MAGIC 0u
#define SC_OFF_VERSION 4u
#define SC_OFF_SEQ 8u          // v2 only
#define SC_OFF_PAYLOAD_V2 12u  // v2 payload starts here
#define SC_OFF_PAYLOAD_V1 8u   // v1 payload started right after version

// Full size of a current (v2) record in bytes.
#define SETTINGS_RECORD_LEN (SC_OFF_PAYLOAD_V2 + sizeof(settings_t) + 4u)

// Encode a v2 record into `rec` (must hold >= SETTINGS_RECORD_LEN bytes).
// Returns the number of bytes written.
size_t settings_codec_encode(uint8_t *rec, const settings_t *s, uint32_t seq);

// Decode a v2 record. Returns true and fills *out / *seq_out only if magic,
// version and CRC all check out; leaves them untouched otherwise.
bool settings_codec_decode(const uint8_t *rec, settings_t *out, uint32_t *seq_out);

// Decode a legacy v1 record (no seq, payload at offset 8). Returns true on a
// valid v1 record.
bool settings_codec_decode_v1(const uint8_t *rec, settings_t *out);

#endif  // DUTLER_SETTINGS_CODEC_H
