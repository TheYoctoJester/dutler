// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// SDK-backed flash port for the whole Pico family (RP2040 + RP2350). The flash
// API and 4 KB/256 B geometry are identical across both; only the total size
// differs, reported at runtime by flash_port_size().
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "platform/flash_port.h"

// Keep the build-time geometry honest against the SDK's notion of this chip.
_Static_assert(FLASH_PORT_SECTOR_SIZE == FLASH_SECTOR_SIZE, "sector size mismatch vs SDK");
_Static_assert(FLASH_PORT_PAGE_SIZE == FLASH_PAGE_SIZE, "page size mismatch vs SDK");

uint32_t flash_port_size(void) { return PICO_FLASH_SIZE_BYTES; }

const uint8_t *flash_port_read(uint32_t off) { return (const uint8_t *)(XIP_BASE + off); }

void flash_port_erase_sector(uint32_t off) {
    // Full watchdog budget for the interrupts-masked erase.
    watchdog_update();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, FLASH_PORT_SECTOR_SIZE);
    restore_interrupts(ints);
}

void flash_port_write_sector(uint32_t off, const uint8_t *page) {
    // Erase + program in one critical section: no IRQ (which would run from
    // flash) executes between erasing the sector and reprogramming its page.
    watchdog_update();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, FLASH_PORT_SECTOR_SIZE);
    flash_range_program(off, page, FLASH_PORT_PAGE_SIZE);
    restore_interrupts(ints);
}
