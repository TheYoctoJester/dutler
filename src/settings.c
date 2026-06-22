// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "settings.h"

#include <string.h>

#include "config.h"
#include "flash_port.h"
#include "settings_codec.h"

settings_t g_settings;

/*
 * ============================================================================
 *  ON-FLASH SETTINGS  —  A/B (ping-pong), power-loss & wear safe
 * ============================================================================
 *
 * Two flash sectors (the last two 4 KB sectors) act as alternating slots. The
 * record format and (de)serialization live in settings_codec.{c,h}; this file
 * owns the flash I/O and slot selection.
 *
 * save() always writes the *inactive* slot and bumps the sequence number, then
 * verifies before declaring success. The active slot is never erased — so a
 * failed or power-interrupted save cannot lose the last good config: load()
 * falls back to the older slot (the half-written one fails its CRC).
 *
 * Legacy: version 1 was a single record (no seq) in the last sector. If no valid
 * v2 record exists, load() reads a v1 record from there and upgrades it to v2
 * (preserving names + baud), then re-saves.
 * ============================================================================
 */

// Two alternating slots: the last two sectors of flash. The v1 record lived in
// the very last sector, which is slot B here, so legacy data is found in place.
#define SLOT_A_OFFSET (FLASH_PORT_TOTAL_SIZE - 2u * FLASH_PORT_SECTOR_SIZE)
#define SLOT_B_OFFSET (FLASH_PORT_TOTAL_SIZE - 1u * FLASH_PORT_SECTOR_SIZE)
#define LEGACY_OFFSET SLOT_B_OFFSET

// A record must fit in one programmable flash page.
_Static_assert(SETTINGS_RECORD_LEN <= FLASH_PORT_PAGE_SIZE,
               "settings record exceeds one flash page");

// 0 = slot A, 1 = slot B. Tracks which slot holds the freshest record so save()
// writes the other one. g_seq is that record's sequence number.
static uint8_t active_slot;
static uint32_t g_seq;

// Force every output name to be NUL-terminated. Defense-in-depth: the write path
// always terminates and the CRC guards corruption, but downstream strlen/strcmp/
// "%s" must never over-read past a name field. Call after any load.
static void terminate_names(void) {
    for (int i = 0; i < OUT_COUNT; i++) g_settings.out_name[i][OUT_NAME_MAX - 1] = '\0';
}

static void load_defaults(void) {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.baud = BRIDGE_INIT_BAUD;
    g_settings.data_bits = 8;
    g_settings.parity = 0;
    g_settings.stop_bits = 1;
    // out_name[] left as empty strings
}

void settings_load(void) {
    const uint8_t *a = flash_port_read(SLOT_A_OFFSET);
    const uint8_t *b = flash_port_read(SLOT_B_OFFSET);

    settings_t cand;
    uint32_t seq, best = 0;
    bool have = false;

    // Pick the valid slot with the highest sequence number. g_settings is
    // assigned only for the candidate that actually wins.
    if (settings_codec_decode(a, &cand, &seq)) {
        g_settings = cand;
        best = seq;
        active_slot = 0;
        have = true;
    }
    if (settings_codec_decode(b, &cand, &seq) && (!have || seq > best)) {
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
    if (settings_codec_decode_v1(flash_port_read(LEGACY_OFFSET), &cand)) {
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

    uint8_t page[FLASH_PORT_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    settings_codec_encode(page, &g_settings, seq);

    flash_port_write_sector(off, page);

    // Verify the freshly written slot before it counts. On failure the other
    // (still-intact) slot stays freshest, so no config is lost. Reads into a
    // scratch buffer — g_settings is never disturbed.
    settings_t chk;
    uint32_t chk_seq;
    if (!settings_codec_decode(flash_port_read(off), &chk, &chk_seq) || chk_seq != seq)
        return false;

    g_seq = seq;
    active_slot = target;
    return true;
}

void settings_reset(void) {
    // Erase both slots so the next boot finds no record and uses defaults.
    flash_port_erase_sector(SLOT_A_OFFSET);
    flash_port_erase_sector(SLOT_B_OFFSET);
    load_defaults();
    g_seq = 0;
    active_slot = 1;  // next save targets slot A
}
