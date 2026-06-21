// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

#include "settings_codec.h"

#include <stddef.h>
#include <string.h>

#include "crc32.h"

// ---------------------------------------------------------------------------
//  Frozen per-version payload snapshots. NEVER edit an existing settings_v<N>_t.
//  The live settings_t is byte-identical to the newest snapshot (asserted).
//  To evolve: append-only fields with defaults, snapshot the old layout here,
//  bump SETTINGS_VERSION, add a migrator.
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t baud;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
    char out_name[OUT_COUNT][OUT_NAME_MAX];
    uint8_t reserved;
} settings_v1_t;

_Static_assert(sizeof(settings_t) == sizeof(settings_v1_t),
               "live settings_t diverged from the latest frozen snapshot: "
               "snapshot the old layout as settings_v<N>_t and bump SETTINGS_VERSION");

// No implicit padding; fields at their stored offsets.
_Static_assert(sizeof(settings_v1_t) == 4u + 1u + 1u + 1u + (OUT_COUNT * OUT_NAME_MAX) + 1u,
               "implicit padding crept into settings_t");
_Static_assert(offsetof(settings_v1_t, baud) == 0, "baud must stay at offset 0");
_Static_assert(offsetof(settings_v1_t, out_name) == 7,
               "out_name offset changed: this breaks every stored record");

static uint32_t rd_u32(const uint8_t *base, size_t off) {
    uint32_t v;
    memcpy(&v, base + off, sizeof(v));
    return v;
}

size_t settings_codec_encode(uint8_t *rec, const settings_t *s, uint32_t seq) {
    uint32_t magic = SETTINGS_MAGIC, version = SETTINGS_VERSION;
    memcpy(rec + SC_OFF_MAGIC, &magic, sizeof(magic));
    memcpy(rec + SC_OFF_VERSION, &version, sizeof(version));
    memcpy(rec + SC_OFF_SEQ, &seq, sizeof(seq));
    memcpy(rec + SC_OFF_PAYLOAD_V2, s, sizeof(*s));
    size_t crc_off = SC_OFF_PAYLOAD_V2 + sizeof(*s);
    uint32_t crc = dutler_crc32(rec, crc_off);
    memcpy(rec + crc_off, &crc, sizeof(crc));
    return crc_off + sizeof(crc);
}

bool settings_codec_decode(const uint8_t *rec, settings_t *out, uint32_t *seq_out) {
    if (rd_u32(rec, SC_OFF_MAGIC) != SETTINGS_MAGIC) return false;
    if (rd_u32(rec, SC_OFF_VERSION) != SETTINGS_VERSION) return false;
    size_t crc_off = SC_OFF_PAYLOAD_V2 + sizeof(settings_t);
    if (dutler_crc32(rec, crc_off) != rd_u32(rec, crc_off)) return false;
    *seq_out = rd_u32(rec, SC_OFF_SEQ);
    memcpy(out, rec + SC_OFF_PAYLOAD_V2, sizeof(settings_t));
    return true;
}

bool settings_codec_decode_v1(const uint8_t *rec, settings_t *out) {
    if (rd_u32(rec, SC_OFF_MAGIC) != SETTINGS_MAGIC) return false;
    if (rd_u32(rec, SC_OFF_VERSION) != 1u) return false;
    size_t crc_off = SC_OFF_PAYLOAD_V1 + sizeof(settings_t);
    if (dutler_crc32(rec, crc_off) != rd_u32(rec, crc_off)) return false;
    memcpy(out, rec + SC_OFF_PAYLOAD_V1, sizeof(settings_t));
    return true;
}
