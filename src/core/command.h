// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef COMMAND_H
#define COMMAND_H

#include <stddef.h>  // size_t

// Control-port command interpreter. Tokenizes one newline-terminated command
// line (modified in place via strtok_r) and dispatches to the matching handler.
void command_dispatch(char *line);

// Context-aware Tab completion provider for the interactive editor (lineedit.c).
// Given the line and cursor, fills out[] with up to `max` candidate strings for
// the token at the cursor (command names, set/get keys, enum values, output
// names) and returns the count. The returned pointers are stable (string
// literals or g_settings storage). Matches the lineedit_complete_fn signature.
size_t command_complete(const char *line, size_t cursor, const char **out, size_t max);

#endif  // COMMAND_H
