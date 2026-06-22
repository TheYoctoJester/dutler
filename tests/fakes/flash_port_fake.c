// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// RAM-backed implementation of flash_port.h for host tests. Lets settings.c
// exercise its A/B slot logic, CRC fallback, migration and power-loss handling
// with no hardware.
#include <string.h>

#include "fakes.h"
#include "flash_port.h"

static uint8_t flash[FLASH_PORT_TOTAL_SIZE];
static bool fail_next_program;

void flash_fake_reset(void) {
    memset(flash, 0xFF, sizeof(flash));
    fail_next_program = false;
}

void flash_fake_poke(uint32_t off, const uint8_t *data, uint32_t n) {
    memcpy(&flash[off], data, n);
}

void flash_fake_fail_next_program(void) { fail_next_program = true; }

const uint8_t *flash_port_read(uint32_t off) { return &flash[off]; }

void flash_port_erase_sector(uint32_t off) { memset(&flash[off], 0xFF, FLASH_PORT_SECTOR_SIZE); }

void flash_port_write_sector(uint32_t off, const uint8_t *page) {
    memset(&flash[off], 0xFF, FLASH_PORT_SECTOR_SIZE);  // erase
    if (fail_next_program) {
        fail_next_program = false;  // simulate power loss after erase, before program
        return;
    }
    memcpy(&flash[off], page, FLASH_PORT_PAGE_SIZE);
}
