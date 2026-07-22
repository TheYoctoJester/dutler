// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "core/kvstore.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "platform/flash_port.h"
#include "util/crc32.h"

/*
 * ============================================================================
 *  USER KEY/VALUE STORE  —  A/B (ping-pong), power-loss & wear safe
 * ============================================================================
 *
 * A self-contained, variable-length store, independent of the fixed settings
 * record. Two flash sectors (the two just below the settings slots) alternate as
 * A/B slots. Each record is:
 *
 *   +--------+---------+--------+-----------+----------------------+-------+
 *   | magic  | version |  seq   | paylen    | payload (paylen B)   |  crc  |
 *   | u32 @0 | u32 @4  | u32 @8 | u32 @12   | @16 : key\0value\0.. | u32   |
 *   +--------+---------+--------+-----------+----------------------+-------+
 *
 * crc is CRC-32 over [magic .. end of payload]. save() writes the *inactive*
 * slot with seq+1 and verifies the read-back before it counts, so a power loss
 * mid-write cannot lose the last good store. Edits stage in the `store` RAM
 * buffer; kv_save() persists it and clears `dirty`.
 */

#define KV_MAGIC 0x3153564Bu  // "KVS1" — frozen
#define KV_VERSION 1u

#define KV_OFF_MAGIC 0u
#define KV_OFF_VERSION 4u
#define KV_OFF_SEQ 8u
#define KV_OFF_PAYLEN 12u
#define KV_OFF_PAYLOAD 16u

// Largest on-flash record, rounded up to a whole flash page (flash_range_program
// needs a page multiple). Must fit one sector.
#define KV_RECORD_MAX                                                                              \
    (((KV_OFF_PAYLOAD + KV_STORE_BYTES + 4u) + FLASH_PORT_PAGE_SIZE - 1u) / FLASH_PORT_PAGE_SIZE * \
     FLASH_PORT_PAGE_SIZE)
_Static_assert(KV_RECORD_MAX <= FLASH_PORT_SECTOR_SIZE, "kv record exceeds one flash sector");

// Two slots, the two sectors just below the settings A/B slots (which are the
// last two). Offsets derive at runtime from the board's flash size.
static uint32_t kv_slot_a(void) { return flash_port_size() - 4u * FLASH_PORT_SECTOR_SIZE; }
static uint32_t kv_slot_b(void) { return flash_port_size() - 3u * FLASH_PORT_SECTOR_SIZE; }

// RAM working copy: packed "key\0value\0" entries; store_len bytes used.
static char store[KV_STORE_BYTES];
static size_t store_len;
static bool dirty;

// A/B bookkeeping (0 = slot A, 1 = slot B) + the active record's sequence.
static uint8_t active_slot;
static uint32_t g_seq;

// Scratch for composing a record before programming it.
static uint8_t rec_buf[KV_RECORD_MAX];

static uint32_t rd_u32(const uint8_t *base, size_t off) {
    uint32_t v;
    memcpy(&v, base + off, sizeof(v));
    return v;
}

static void wr_u32(uint8_t *base, size_t off, uint32_t v) { memcpy(base + off, &v, sizeof(v)); }

// Validate a slot's record; on success return its seq and payload length.
static bool slot_valid(const uint8_t *rec, uint32_t *seq_out, uint32_t *paylen_out) {
    if (rd_u32(rec, KV_OFF_MAGIC) != KV_MAGIC) return false;
    if (rd_u32(rec, KV_OFF_VERSION) != KV_VERSION) return false;
    uint32_t paylen = rd_u32(rec, KV_OFF_PAYLEN);
    if (paylen > KV_STORE_BYTES) return false;
    size_t crc_off = KV_OFF_PAYLOAD + paylen;
    if (dutler_crc32(rec, crc_off) != rd_u32(rec, crc_off)) return false;
    *seq_out = rd_u32(rec, KV_OFF_SEQ);
    *paylen_out = paylen;
    return true;
}

// Find `key` in the working copy. On hit, *off = its start, *len = full entry
// length (key + NUL + value + NUL). Returns true on hit.
static bool find(const char *key, size_t *off, size_t *len) {
    size_t i = 0;
    while (i < store_len) {
        size_t klen = strlen(&store[i]) + 1;
        size_t vlen = strlen(&store[i + klen]) + 1;
        if (strcmp(&store[i], key) == 0) {
            *off = i;
            *len = klen + vlen;
            return true;
        }
        i += klen + vlen;
    }
    return false;
}

void kv_load(void) {
    const uint8_t *slot[2] = {flash_port_read(kv_slot_a()), flash_port_read(kv_slot_b())};
    bool have = false;
    uint32_t best = 0, best_paylen = 0;
    uint8_t from = 1;
    const uint8_t *best_rec = NULL;

    for (uint8_t i = 0; i < 2; i++) {
        uint32_t seq, paylen;
        if (slot_valid(slot[i], &seq, &paylen) && (!have || seq > best)) {
            have = true;
            best = seq;
            best_paylen = paylen;
            from = i;
            best_rec = slot[i];
        }
    }

    if (have) {
        memcpy(store, best_rec + KV_OFF_PAYLOAD, best_paylen);
        store_len = best_paylen;
        g_seq = best;
        active_slot = from;
    } else {
        store_len = 0;
        g_seq = 0;
        active_slot = 1;  // first save targets slot A
    }
    dirty = false;
}

bool kv_save(void) {
    uint8_t target = active_slot ^ 1u;  // write the *inactive* slot
    uint32_t off = target ? kv_slot_b() : kv_slot_a();
    uint32_t seq = g_seq + 1u;

    size_t rec_len = KV_OFF_PAYLOAD + store_len + 4u;
    size_t prog_len =
        (rec_len + FLASH_PORT_PAGE_SIZE - 1u) / FLASH_PORT_PAGE_SIZE * FLASH_PORT_PAGE_SIZE;
    memset(rec_buf, 0xFF, prog_len);
    wr_u32(rec_buf, KV_OFF_MAGIC, KV_MAGIC);
    wr_u32(rec_buf, KV_OFF_VERSION, KV_VERSION);
    wr_u32(rec_buf, KV_OFF_SEQ, seq);
    wr_u32(rec_buf, KV_OFF_PAYLEN, (uint32_t)store_len);
    memcpy(rec_buf + KV_OFF_PAYLOAD, store, store_len);
    uint32_t crc = dutler_crc32(rec_buf, KV_OFF_PAYLOAD + store_len);
    wr_u32(rec_buf, KV_OFF_PAYLOAD + store_len, crc);

    flash_port_write_sector(off, rec_buf, (uint32_t)prog_len);

    // Verify the freshly written slot before it counts; on failure the other
    // slot stays freshest, so nothing is lost.
    uint32_t vseq, vpaylen;
    if (!slot_valid(flash_port_read(off), &vseq, &vpaylen) || vseq != seq) return false;

    g_seq = seq;
    active_slot = target;
    dirty = false;
    return true;
}

void kv_reset(void) {
    flash_port_erase_sector(kv_slot_a());
    flash_port_erase_sector(kv_slot_b());
    store_len = 0;
    g_seq = 0;
    active_slot = 1;
    dirty = false;
}

bool kv_dirty(void) { return dirty; }

const char *kv_get(const char *key) {
    size_t off, len;
    if (!find(key, &off, &len)) return NULL;
    return &store[off + strlen(&store[off]) + 1];  // value follows key + NUL
}

bool kv_set(const char *key, const char *value) {
    size_t klen = strlen(key) + 1;
    size_t vlen = strlen(value) + 1;
    if (klen > KV_KEY_MAX || vlen > KV_VALUE_MAX) return false;

    size_t off, elen;
    bool exists = find(key, &off, &elen);
    size_t new_len = store_len - (exists ? elen : 0) + klen + vlen;
    if (new_len > KV_STORE_BYTES) return false;  // full — leave the store untouched

    if (exists) {  // drop the old entry so the new one replaces it
        memmove(&store[off], &store[off + elen], store_len - (off + elen));
        store_len -= elen;
    }
    memcpy(&store[store_len], key, klen);
    store_len += klen;
    memcpy(&store[store_len], value, vlen);
    store_len += vlen;
    dirty = true;
    return true;
}

bool kv_clear(const char *key) {
    size_t off, len;
    if (!find(key, &off, &len)) return false;
    memmove(&store[off], &store[off + len], store_len - (off + len));
    store_len -= len;
    dirty = true;
    return true;
}

void kv_foreach(void (*cb)(const char *key, const char *value)) {
    size_t i = 0;
    while (i < store_len) {
        const char *k = &store[i];
        size_t klen = strlen(k) + 1;
        const char *v = &store[i + klen];
        size_t vlen = strlen(v) + 1;
        cb(k, v);
        i += klen + vlen;
    }
}
