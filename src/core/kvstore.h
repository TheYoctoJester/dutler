// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef DUTLER_KVSTORE_H
#define DUTLER_KVSTORE_H

#include <stdbool.h>

#include "config.h"

// User key/value store: arbitrary short strings the user sets over the control
// port (`set kv <key> <value>` / `get kv`), persisted in their own flash A/B
// slots — separate from the built-in settings, so it evolves independently.
//
// Edits stage in a RAM working copy (reported by kv_dirty()); the `save` command
// flushes it to flash via kv_save(). Pure of the SDK: all flash I/O goes through
// platform/flash_port.h, so it is host-testable against the RAM flash fake.

// Load the active flash slot into the RAM working copy (call once at boot).
void kv_load(void);

// Flush the working copy to the inactive slot (A/B, verified read-back before it
// counts). Returns true on success; clears the dirty flag.
bool kv_save(void);

// Erase both slots and empty the working copy (factory reset).
void kv_reset(void);

// True if the working copy has unsaved edits.
bool kv_dirty(void);

// Look up a key; returns a pointer to its value (valid until the next edit) or
// NULL if absent.
const char *kv_get(const char *key);

// Set/replace key=value in the working copy. Returns false (nothing changed) if
// the key or value is too long, or the store is full; marks dirty on success.
bool kv_set(const char *key, const char *value);

// Delete a key from the working copy. Returns true if it existed (marks dirty).
bool kv_clear(const char *key);

// Call cb(key, value) for every entry in the working copy, in stored order.
void kv_foreach(void (*cb)(const char *key, const char *value));

#endif  // DUTLER_KVSTORE_H
