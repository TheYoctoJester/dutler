// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "core/settings_codec.h"

#include <stddef.h>
#include <string.h>

#include "util/crc32.h"

// ---------------------------------------------------------------------------
//  Frozen per-version payload snapshots. NEVER edit an existing settings_v<N>_t.
//  The live settings_t is byte-identical to the newest snapshot (asserted).
//  To evolve: append-only fields with defaults, snapshot the old layout here,
//  bump SETTINGS_VERSION, add a migrator.
// ---------------------------------------------------------------------------
// Payload generation 1 — record versions 1 and 2 (no device_name). FROZEN.
typedef struct {
    uint32_t baud;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
    char out_name[OUT_COUNT][OUT_NAME_MAX];
    uint8_t reserved;  // == echo in the live struct
} settings_v1_t;

// Payload generation 2 — record version 3 (appends device_name). FROZEN once
// shipped; the live settings_t must stay byte-identical to it.
typedef struct {
    uint32_t baud;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
    char out_name[OUT_COUNT][OUT_NAME_MAX];
    uint8_t echo;
    char device_name[DEVICE_NAME_MAX];
} settings_v2_t;

_Static_assert(sizeof(settings_t) == sizeof(settings_v2_t),
               "live settings_t diverged from the latest frozen snapshot: "
               "snapshot the old layout as settings_v<N>_t and bump SETTINGS_VERSION");

// No implicit padding; fields at their stored offsets.
_Static_assert(sizeof(settings_v1_t) == 4u + 1u + 1u + 1u + (OUT_COUNT * OUT_NAME_MAX) + 1u,
               "implicit padding crept into settings_v1_t");
_Static_assert(sizeof(settings_v2_t) ==
                   4u + 1u + 1u + 1u + (OUT_COUNT * OUT_NAME_MAX) + 1u + DEVICE_NAME_MAX,
               "implicit padding crept into settings_t (keep the struct a multiple of 4)");
_Static_assert(offsetof(settings_v2_t, baud) == 0, "baud must stay at offset 0");
_Static_assert(offsetof(settings_v2_t, out_name) == 7,
               "out_name offset changed: this breaks every stored record");
_Static_assert(offsetof(settings_v2_t, device_name) == 4u + 1u + 1u + 1u + (OUT_COUNT * OUT_NAME_MAX) + 1u,
               "device_name offset changed: this breaks every stored v3 record");

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
    memcpy(rec + SC_OFF_PAYLOAD_V3, s, sizeof(*s));
    size_t crc_off = SC_OFF_PAYLOAD_V3 + sizeof(*s);
    uint32_t crc = dutler_crc32(rec, crc_off);
    memcpy(rec + crc_off, &crc, sizeof(crc));
    return crc_off + sizeof(crc);
}

bool settings_codec_decode(const uint8_t *rec, settings_t *out, uint32_t *seq_out) {
    if (rd_u32(rec, SC_OFF_MAGIC) != SETTINGS_MAGIC) return false;
    if (rd_u32(rec, SC_OFF_VERSION) != SETTINGS_VERSION) return false;
    size_t crc_off = SC_OFF_PAYLOAD_V3 + sizeof(settings_t);
    if (dutler_crc32(rec, crc_off) != rd_u32(rec, crc_off)) return false;
    *seq_out = rd_u32(rec, SC_OFF_SEQ);
    memcpy(out, rec + SC_OFF_PAYLOAD_V3, sizeof(settings_t));
    return true;
}

bool settings_codec_decode_v2(const uint8_t *rec, settings_t *out, uint32_t *seq_out) {
    if (rd_u32(rec, SC_OFF_MAGIC) != SETTINGS_MAGIC) return false;
    if (rd_u32(rec, SC_OFF_VERSION) != 2u) return false;
    // v2 payload is the frozen gen-1 layout (no device_name).
    size_t crc_off = SC_OFF_PAYLOAD_V2 + sizeof(settings_v1_t);
    if (dutler_crc32(rec, crc_off) != rd_u32(rec, crc_off)) return false;
    memset(out, 0, sizeof(*out));  // clears the appended device_name
    memcpy(out, rec + SC_OFF_PAYLOAD_V2, sizeof(settings_v1_t));
    *seq_out = rd_u32(rec, SC_OFF_SEQ);
    return true;
}

bool settings_codec_decode_v1(const uint8_t *rec, settings_t *out) {
    if (rd_u32(rec, SC_OFF_MAGIC) != SETTINGS_MAGIC) return false;
    if (rd_u32(rec, SC_OFF_VERSION) != 1u) return false;
    // v1 payload is the frozen gen-1 layout — size it by that snapshot, not the
    // (now larger) live settings_t.
    size_t crc_off = SC_OFF_PAYLOAD_V1 + sizeof(settings_v1_t);
    if (dutler_crc32(rec, crc_off) != rd_u32(rec, crc_off)) return false;
    memset(out, 0, sizeof(*out));  // clears fields absent from the v1 payload
    memcpy(out, rec + SC_OFF_PAYLOAD_V1, sizeof(settings_v1_t));
    return true;
}
