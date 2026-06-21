#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

settings_t g_settings;

#define SETTINGS_MAGIC 0x52454C31u  // "REL1"
#define SETTINGS_VERSION 1u

// Reserve the very last 4 KB sector of flash; the program lives at the start
// and is far smaller, so this never collides.
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint32_t magic;
    uint32_t version;
    settings_t s;
    uint32_t crc;  // over everything preceding this field
} flash_blob_t;

_Static_assert(sizeof(flash_blob_t) <= FLASH_PAGE_SIZE,
               "settings blob exceeds one flash page");

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

void settings_load(void) {
    const flash_blob_t *fb = (const flash_blob_t *)(XIP_BASE + SETTINGS_OFFSET);
    if (fb->magic == SETTINGS_MAGIC && fb->version == SETTINGS_VERSION &&
        fb->crc == crc32(fb, offsetof(flash_blob_t, crc))) {
        g_settings = fb->s;
        return;
    }
    load_defaults();
}

bool settings_save(void) {
    flash_blob_t fb;
    memset(&fb, 0, sizeof(fb));
    fb.magic = SETTINGS_MAGIC;
    fb.version = SETTINGS_VERSION;
    fb.s = g_settings;
    fb.crc = crc32(&fb, offsetof(flash_blob_t, crc));

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &fb, sizeof(fb));

    // Single core here, so masking interrupts is sufficient to make the
    // erase/program safe (no code runs from flash meanwhile).
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    const flash_blob_t *chk = (const flash_blob_t *)(XIP_BASE + SETTINGS_OFFSET);
    return chk->magic == SETTINGS_MAGIC &&
           chk->crc == crc32(chk, offsetof(flash_blob_t, crc));
}
