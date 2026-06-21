#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

settings_t g_settings;

/*
 * ============================================================================
 *  ON-FLASH SETTINGS FORMAT  —  READ THIS BEFORE CHANGING settings_t
 * ============================================================================
 *
 * One record is stored in the reserved flash sector, laid out by byte offset:
 *
 *     +--------+---------+----------------------------+-------+
 *     | magic  | version |   payload (settings_t)     |  crc  |
 *     | u32 @0 | u32 @4  |   P bytes @8               | u32   |
 *     +--------+---------+----------------------------+-------+
 *                                                       @ 8+P
 *
 *   magic    SETTINGS_MAGIC. FROZEN forever; identifies "our" record.
 *   version  SETTINGS_VERSION at write time. Its offset (4) and width (u32)
 *            are FROZEN forever so ANY future build can read it first and then
 *            decide how to interpret the payload.
 *   payload  The settings_t struct exactly as it existed for `version`.
 *            Its size P therefore differs between versions.
 *   crc      CRC32 over bytes [0 .. 8+P). Sits right after the payload, so its
 *            offset depends on that version's P.
 *
 * The reader keys off `version`: the current version is loaded directly; an
 * older known version is MIGRATED to the current layout and re-saved; anything
 * else (bad magic, bad CRC, unknown version) falls back to safe defaults.
 *
 * ----------------------------------------------------------------------------
 *  HOW TO EVOLVE THE LAYOUT  (follow every step when you change settings_t)
 * ----------------------------------------------------------------------------
 *  1. PREFER APPEND-ONLY. Add new fields at the END of settings_t (in
 *     settings.h) and give each a sensible default in load_defaults(). Do NOT
 *     reorder, resize, remove, or repurpose an existing field: old records were
 *     CRC'd over the old byte layout and must remain interpretable as-is.
 *
 *  2. FREEZE the outgoing layout as a snapshot struct, e.g. copy the current
 *     settings_t body into a new `settings_v<N>_t` below. A frozen
 *     settings_v<N>_t must NEVER be edited again.
 *
 *  3. BUMP SETTINGS_VERSION (e.g. 1 -> 2).
 *
 *  4. ADD a migrator (see the worked migrate_v1() template below) and register
 *     it as a `case` in settings_load(). For a pure append-only change the
 *     migrator just copies the old fields and lets load_defaults() populate the
 *     new tail.
 *
 *  5. UPDATE the _Static_assert so it compares the live settings_t against the
 *     NEWEST frozen snapshot — that guarantees the live struct and the latest
 *     on-flash layout can never silently diverge.
 * ============================================================================
 */

#define SETTINGS_MAGIC 0x52454C31u  // "REL1" — FROZEN, never change
#define SETTINGS_VERSION 1u         // bump on every settings_t layout change

// Reserve the very last 4 KB sector of flash; the program lives at the start
// and is far smaller, so this never collides.
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

#define HDR_BYTES 8u   // magic (u32) + version (u32), FROZEN
#define CRC_BYTES 4u

// ---------------------------------------------------------------------------
//  Frozen per-version payload snapshots. NEVER edit an existing settings_v<N>_t;
//  add a new one when you bump the version. v1 is the first released layout and
//  is byte-identical to today's live settings_t.
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t baud;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
    char relay_name[RELAY_COUNT][RELAY_NAME_MAX];
    uint8_t reserved;
} settings_v1_t;

// The live settings_t must always equal the NEWEST frozen snapshot. When you
// add settings_v2_t, change this assert to compare against settings_v2_t.
_Static_assert(sizeof(settings_t) == sizeof(settings_v1_t),
               "live settings_t diverged from the latest frozen snapshot: "
               "snapshot the old layout as settings_v<N>_t and bump SETTINGS_VERSION");

// Enforce the on-flash layout: the struct size must equal the exact sum of its
// fields (i.e. NO implicit/compiler padding), and the fields must sit at the
// offsets the stored records were written with. Any drift breaks the build
// instead of silently mis-reading flash on a future toolchain/edit.
_Static_assert(sizeof(settings_v1_t) ==
                   4u /*baud*/ + 1u /*data_bits*/ + 1u /*parity*/ +
                       1u /*stop_bits*/ + (RELAY_COUNT * RELAY_NAME_MAX) +
                       1u /*reserved*/,
               "implicit padding crept into settings_t: reorder fields or add "
               "explicit reserved bytes to remove the gap");
_Static_assert(offsetof(settings_v1_t, baud) == 0, "baud must stay at offset 0");
_Static_assert(offsetof(settings_v1_t, relay_name) == 7,
               "relay_name offset changed: this breaks every stored record");

// The whole record must fit in a single programmable flash page.
_Static_assert(HDR_BYTES + sizeof(settings_t) + CRC_BYTES <= FLASH_PAGE_SIZE,
               "settings record exceeds one flash page");

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

static void load_defaults(void) {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.baud = BRIDGE_INIT_BAUD;
    g_settings.data_bits = 8;
    g_settings.parity = 0;
    g_settings.stop_bits = 1;
    // relay_name[] left as empty strings
}

// Validate the CRC of a record whose payload is `payload_size` bytes long.
static bool record_crc_ok(const uint8_t *base, size_t payload_size) {
    uint32_t stored;
    memcpy(&stored, base + HDR_BYTES + payload_size, CRC_BYTES);
    return crc32(base, HDR_BYTES + payload_size) == stored;
}

// Load a record written by the CURRENT version directly into g_settings.
static bool load_current(const uint8_t *base) {
    if (!record_crc_ok(base, sizeof(settings_t))) return false;
    memcpy(&g_settings, base + HDR_BYTES, sizeof(settings_t));
    return true;
}

/*
 * ---------------------------------------------------------------------------
 *  MIGRATOR TEMPLATE — keep as the model for real migrators.
 *
 *  When SETTINGS_VERSION becomes 2 and settings_t has grown (append-only),
 *  uncomment and adapt this, then add `case 1:` to settings_load():
 *
 *  static bool migrate_v1(const uint8_t *base) {
 *      if (!record_crc_ok(base, sizeof(settings_v1_t))) return false;  // bad v1
 *      settings_v1_t old;
 *      memcpy(&old, base + HDR_BYTES, sizeof(old));
 *      load_defaults();                       // new trailing fields -> defaults
 *      g_settings.baud      = old.baud;       // carry forward every old field
 *      g_settings.data_bits = old.data_bits;
 *      g_settings.parity    = old.parity;
 *      g_settings.stop_bits = old.stop_bits;
 *      memcpy(g_settings.relay_name, old.relay_name, sizeof(old.relay_name));
 *      return true;
 *  }
 * ---------------------------------------------------------------------------
 */

void settings_load(void) {
    const uint8_t *base = (const uint8_t *)(XIP_BASE + SETTINGS_OFFSET);

    uint32_t magic, version;
    memcpy(&magic, base, sizeof(magic));
    memcpy(&version, base + 4, sizeof(version));

    if (magic == SETTINGS_MAGIC) {
        switch (version) {
            case SETTINGS_VERSION:
                if (load_current(base)) return;
                break;
            // Add older versions here, newest-first, e.g.:
            // case 1:
            //     if (migrate_v1(base)) { settings_save(); return; }
            //     break;
            default:
                break;  // unknown/newer version -> defaults
        }
    }

    load_defaults();  // absent, corrupt, or unmigratable -> safe defaults
}

bool settings_save(void) {
    // Build the record at explicit offsets so the byte layout is independent of
    // C struct padding around the header/crc (the payload keeps its own layout).
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));

    uint32_t magic = SETTINGS_MAGIC;
    uint32_t version = SETTINGS_VERSION;
    size_t off = 0;
    memcpy(page + off, &magic, sizeof(magic));
    off += sizeof(magic);
    memcpy(page + off, &version, sizeof(version));
    off += sizeof(version);
    memcpy(page + off, &g_settings, sizeof(g_settings));
    off += sizeof(g_settings);
    uint32_t crc = crc32(page, off);
    memcpy(page + off, &crc, sizeof(crc));

    // Single core here, so masking interrupts is sufficient to make the
    // erase/program safe (no code runs from flash meanwhile).
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    const uint8_t *base = (const uint8_t *)(XIP_BASE + SETTINGS_OFFSET);
    return record_crc_ok(base, sizeof(settings_t));
}

void settings_reset(void) {
    // Erase the record so the next boot also sees a blank sector -> defaults.
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    load_defaults();
}
