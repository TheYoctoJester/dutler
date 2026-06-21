// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

settings_t g_settings;

/*
 * ============================================================================
 *  ON-FLASH SETTINGS  —  A/B (ping-pong), power-loss & wear safe
 * ============================================================================
 *
 * Two flash sectors (the last two 4 KB sectors) act as alternating slots.
 * Each record is laid out by byte offset:
 *
 *   +--------+---------+--------+----------------------------+-------+
 *   | magic  | version |  seq   |   payload (settings_t)     |  crc  |
 *   | u32 @0 | u32 @4  | u32 @8 |   P bytes @12              | u32   |
 *   +--------+---------+--------+----------------------------+-------+
 *
 *   magic    SETTINGS_MAGIC, frozen, identifies "our" record.
 *   version  SETTINGS_VERSION. Offset (4) and width (u32) frozen forever so any
 *            build can read it first and pick the right layout.
 *   seq      monotonic counter. load() picks the valid slot with the highest seq.
 *   crc      CRC32 over [magic .. end of payload].
 *
 * save() always writes the *inactive* slot and bumps seq, then verifies before
 * declaring success. The active slot is never erased — so a failed or
 * power-interrupted save cannot lose the last good config: load() falls back to
 * the older slot (the half-written one fails its CRC and is ignored).
 *
 * Legacy: version 1 was a single record (no seq, payload @8) in the last sector.
 * If no valid v2 record exists, load() reads a v1 record from there and upgrades
 * it to v2 (preserving names + baud), then re-saves.
 *
 * Evolving settings_t (the payload) is independent of this A/B framing: same
 * rules as before — append-only fields with defaults, freeze the old layout as
 * settings_v<N>_t, bump SETTINGS_VERSION, migrate. See the snapshot + asserts.
 * ============================================================================
 */

#define SETTINGS_MAGIC 0x52454C31u  // "REL1" — frozen
#define SETTINGS_VERSION 2u         // 2 = A/B with seq; 1 = legacy single slot

// Record field offsets. magic/version are frozen across versions.
#define OFF_MAGIC 0u
#define OFF_VERSION 4u
#define OFF_SEQ 8u           // v2 only
#define OFF_PAYLOAD_V2 12u   // v2 payload starts here
#define OFF_PAYLOAD_V1 8u    // v1 payload started right after version

// Two alternating slots: the last two sectors of flash. The v1 record lived in
// the very last sector, which is slot B here, so legacy data is found in place.
#define SLOT_A_OFFSET (PICO_FLASH_SIZE_BYTES - 2u * FLASH_SECTOR_SIZE)
#define SLOT_B_OFFSET (PICO_FLASH_SIZE_BYTES - 1u * FLASH_SECTOR_SIZE)
#define LEGACY_OFFSET SLOT_B_OFFSET

// ---------------------------------------------------------------------------
//  Frozen per-version payload snapshots. NEVER edit an existing settings_v<N>_t.
//  The live settings_t is byte-identical to the newest snapshot (asserted).
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t baud;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
    char relay_name[RELAY_COUNT][RELAY_NAME_MAX];
    uint8_t reserved;
} settings_v1_t;

_Static_assert(sizeof(settings_t) == sizeof(settings_v1_t),
               "live settings_t diverged from the latest frozen snapshot: "
               "snapshot the old layout as settings_v<N>_t and bump SETTINGS_VERSION");

// No implicit padding, fields at their stored offsets.
_Static_assert(sizeof(settings_v1_t) ==
                   4u + 1u + 1u + 1u + (RELAY_COUNT * RELAY_NAME_MAX) + 1u,
               "implicit padding crept into settings_t");
_Static_assert(offsetof(settings_v1_t, baud) == 0, "baud must stay at offset 0");
_Static_assert(offsetof(settings_v1_t, relay_name) == 7,
               "relay_name offset changed: this breaks every stored record");

// The largest record (v2) must fit in one programmable flash page.
_Static_assert(OFF_PAYLOAD_V2 + sizeof(settings_t) + 4u <= FLASH_PAGE_SIZE,
               "settings record exceeds one flash page");

// 0 = slot A, 1 = slot B. Tracks which slot holds the freshest record so save()
// writes the other one. g_seq is that record's sequence number.
static uint8_t active_slot;
static uint32_t g_seq;

static uint32_t crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static uint32_t rd_u32(const uint8_t *base, size_t off) {
    uint32_t v;
    memcpy(&v, base + off, sizeof(v));
    return v;
}

// Force every relay name to be NUL-terminated. Defense-in-depth: the write path
// always terminates and the CRC guards corruption, but downstream strlen/strcmp/
// "%s" must never over-read past a name field. Call after any load.
static void terminate_names(void) {
    for (int i = 0; i < RELAY_COUNT; i++)
        g_settings.relay_name[i][RELAY_NAME_MAX - 1] = '\0';
}

static void load_defaults(void) {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.baud = BRIDGE_INIT_BAUD;
    g_settings.data_bits = 8;
    g_settings.parity = 0;
    g_settings.stop_bits = 1;
    // relay_name[] left as empty strings
}

// Read a current-version (v2) record from a slot into *out and its sequence into
// *seq_out. Pure: touches no globals; leaves *out untouched on any failure.
static bool read_v2(const uint8_t *base, settings_t *out, uint32_t *seq_out) {
    if (rd_u32(base, OFF_MAGIC) != SETTINGS_MAGIC) return false;
    if (rd_u32(base, OFF_VERSION) != SETTINGS_VERSION) return false;
    size_t crc_off = OFF_PAYLOAD_V2 + sizeof(settings_t);
    if (crc32(base, crc_off) != rd_u32(base, crc_off)) return false;
    *seq_out = rd_u32(base, OFF_SEQ);
    memcpy(out, base + OFF_PAYLOAD_V2, sizeof(settings_t));
    return true;
}

// Read a legacy (v1) record into *out. Same payload struct, no seq, payload at
// offset 8. Pure, like read_v2.
static bool read_v1(const uint8_t *base, settings_t *out) {
    if (rd_u32(base, OFF_MAGIC) != SETTINGS_MAGIC) return false;
    if (rd_u32(base, OFF_VERSION) != 1u) return false;
    size_t crc_off = OFF_PAYLOAD_V1 + sizeof(settings_t);
    if (crc32(base, crc_off) != rd_u32(base, crc_off)) return false;
    memcpy(out, base + OFF_PAYLOAD_V1, sizeof(settings_t));
    return true;
}

void settings_load(void) {
    const uint8_t *a = (const uint8_t *)(XIP_BASE + SLOT_A_OFFSET);
    const uint8_t *b = (const uint8_t *)(XIP_BASE + SLOT_B_OFFSET);

    settings_t cand;
    uint32_t seq, best = 0;
    bool have = false;

    // Pick the valid slot with the highest sequence number. g_settings is
    // assigned only for the candidate that actually wins.
    if (read_v2(a, &cand, &seq)) {
        g_settings = cand;
        best = seq;
        active_slot = 0;
        have = true;
    }
    if (read_v2(b, &cand, &seq) && (!have || seq > best)) {
        g_settings = cand;
        best = seq;
        active_slot = 1;
        have = true;
    }

    if (have) {
        g_seq = best;
        terminate_names();
        return;
    }

    // No valid v2 record: upgrade a legacy v1 record in place if present.
    if (read_v1((const uint8_t *)(XIP_BASE + LEGACY_OFFSET), &cand)) {
        g_settings = cand;
        terminate_names();
        g_seq = 0;
        active_slot = 1;  // pretend B is active so the upgrade writes slot A
        settings_save();  // persist as v2 (best-effort; retried next boot)
        return;
    }

    load_defaults();
    g_seq = 0;
    active_slot = 1;  // first save targets slot A
}

bool settings_save(void) {
    uint8_t target = active_slot ^ 1u;  // write the *inactive* slot
    uint32_t off = target ? SLOT_B_OFFSET : SLOT_A_OFFSET;
    uint32_t seq = g_seq + 1u;

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));

    uint32_t magic = SETTINGS_MAGIC, version = SETTINGS_VERSION;
    memcpy(page + OFF_MAGIC, &magic, sizeof(magic));
    memcpy(page + OFF_VERSION, &version, sizeof(version));
    memcpy(page + OFF_SEQ, &seq, sizeof(seq));
    memcpy(page + OFF_PAYLOAD_V2, &g_settings, sizeof(g_settings));
    size_t crc_off = OFF_PAYLOAD_V2 + sizeof(g_settings);
    uint32_t crc = crc32(page, crc_off);
    memcpy(page + crc_off, &crc, sizeof(crc));

    // Full watchdog budget for the interrupts-masked erase/program.
    watchdog_update();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, FLASH_SECTOR_SIZE);
    flash_range_program(off, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    // Verify the freshly written slot before it counts. On failure the other
    // (still-intact) slot stays freshest, so no config is lost. Reads into a
    // scratch buffer — g_settings is never disturbed.
    settings_t chk;
    uint32_t chk_seq;
    if (!read_v2((const uint8_t *)(XIP_BASE + off), &chk, &chk_seq) || chk_seq != seq)
        return false;

    g_seq = seq;
    active_slot = target;
    return true;
}

void settings_reset(void) {
    // Erase both slots so the next boot finds no record and uses defaults.
    watchdog_update();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SLOT_A_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_erase(SLOT_B_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    load_defaults();
    g_seq = 0;
    active_slot = 1;  // next save targets slot A
}
