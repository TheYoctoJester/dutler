// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef DUTLER_FLASH_PORT_H
#define DUTLER_FLASH_PORT_H

#include <stdint.h>

// Hardware seam for the persistent-settings store. settings.c owns the slot
// logic (A/B selection, sequence numbers, CRC, migration) and talks to flash
// only through these three calls — so it builds and runs on the host against a
// RAM-backed fake (tests/fakes/flash_port_fake.c). The RP2040 implementation is
// flash_port_rp2040.c; exactly one implementation is linked per executable.
//
// Geometry is fixed at build time (a stock Pico's W25Q16, 2 MB, 4 KB sectors,
// 256 B pages). flash_port_rp2040.c static-asserts these against the SDK macros.
#define FLASH_PORT_SECTOR_SIZE 4096u
#define FLASH_PORT_PAGE_SIZE 256u
#define FLASH_PORT_TOTAL_SIZE (2u * 1024u * 1024u)

// Read-only view of flash at byte offset `off` (memory-mapped XIP on the target).
const uint8_t *flash_port_read(uint32_t off);

// Erase the FLASH_PORT_SECTOR_SIZE sector at `off` (must be sector-aligned).
void flash_port_erase_sector(uint32_t off);

// Erase the sector at `off`, then program FLASH_PORT_PAGE_SIZE bytes from `page`
// into its start — as one atomic operation (target: a single interrupts-masked
// critical section, so no code runs from flash mid-write).
void flash_port_write_sector(uint32_t off, const uint8_t *page);

#endif  // DUTLER_FLASH_PORT_H
