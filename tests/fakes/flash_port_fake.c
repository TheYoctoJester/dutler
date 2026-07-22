// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// RAM-backed implementation of flash_port.h for host tests. Lets settings.c
// exercise its A/B slot logic, CRC fallback, migration and power-loss handling
// with no hardware.
#include <assert.h>
#include <string.h>

#include "fakes.h"
#include "platform/flash_port.h"

// Host-side flash region. The backing store is sized for the largest board we
// build for (Pico 2, 4 MB); the *reported* size is runtime-settable so tests can
// exercise the board-dependent slot arithmetic at more than one geometry (a fixed
// size would only ever test the offsets at that single value). Defaults to a 2 MB
// Pico after each reset.
#define FAKE_FLASH_MAX (4u * 1024u * 1024u)
#define FAKE_FLASH_DEFAULT (2u * 1024u * 1024u)

static uint8_t flash[FAKE_FLASH_MAX];
static uint32_t flash_size = FAKE_FLASH_DEFAULT;
static bool fail_next_program;

uint32_t flash_port_size(void) { return flash_size; }

void flash_fake_set_size(uint32_t bytes) {
    // Must fit the backing store and be sector-aligned (real boards always are;
    // settings.c derives sector-aligned slot offsets and would otherwise misalign).
    assert(bytes <= FAKE_FLASH_MAX && bytes % FLASH_PORT_SECTOR_SIZE == 0);
    flash_size = bytes;
}

void flash_fake_reset(void) {
    memset(flash, 0xFF, sizeof(flash));
    flash_size = FAKE_FLASH_DEFAULT;  // isolate tests that change the size
    fail_next_program = false;
}

void flash_fake_poke(uint32_t off, const uint8_t *data, uint32_t n) {
    memcpy(&flash[off], data, n);
}

void flash_fake_fail_next_program(void) { fail_next_program = true; }

const uint8_t *flash_port_read(uint32_t off) { return &flash[off]; }

void flash_port_erase_sector(uint32_t off) { memset(&flash[off], 0xFF, FLASH_PORT_SECTOR_SIZE); }

void flash_port_write_sector(uint32_t off, const uint8_t *buf, uint32_t len) {
    memset(&flash[off], 0xFF, FLASH_PORT_SECTOR_SIZE);  // erase
    if (fail_next_program) {
        fail_next_program = false;  // simulate power loss after erase, before program
        return;
    }
    memcpy(&flash[off], buf, len);
}
