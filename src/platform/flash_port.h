// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef DUTLER_FLASH_PORT_H
#define DUTLER_FLASH_PORT_H

#include <stdint.h>

// Hardware seam for the persistent-settings store. settings.c owns the slot
// logic (A/B selection, sequence numbers, CRC, migration) and talks to flash
// only through these three calls — so it builds and runs on the host against a
// RAM-backed fake (tests/fakes/flash_port_fake.c). The on-target implementation
// is flash_port_pico.c; exactly one implementation is linked per executable.
//
// Sector/page geometry is the same across the RP2040 and RP2350 Pico family
// (4 KB sectors, 256 B pages); flash_port_pico.c static-asserts these against the
// SDK. Total flash size is board-dependent (2 MB on a Pico, 4 MB on a Pico 2), so
// it is a runtime query instead of a macro.
#define FLASH_PORT_SECTOR_SIZE 4096u
#define FLASH_PORT_PAGE_SIZE 256u

// Total flash size in bytes (board-dependent on the target; the fake's region on
// the host). settings.c derives its A/B slot offsets from this.
uint32_t flash_port_size(void);

// Read-only view of flash at byte offset `off` (memory-mapped XIP on the target).
const uint8_t *flash_port_read(uint32_t off);

// Erase the FLASH_PORT_SECTOR_SIZE sector at `off` (must be sector-aligned).
void flash_port_erase_sector(uint32_t off);

// Erase the sector at `off`, then program `len` bytes from `buf` into its start —
// as one atomic operation (target: a single interrupts-masked critical section,
// so no code runs from flash mid-write). `len` must be a multiple of
// FLASH_PORT_PAGE_SIZE and <= FLASH_PORT_SECTOR_SIZE (the settings store writes one
// page; the KV store writes several).
void flash_port_write_sector(uint32_t off, const uint8_t *buf, uint32_t len);

#endif  // DUTLER_FLASH_PORT_H
