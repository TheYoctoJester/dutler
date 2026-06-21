// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef COMMAND_H
#define COMMAND_H

// Control-port command interpreter. Tokenizes one newline-terminated command
// line (modified in place via strtok_r) and dispatches to the matching handler.
void command_dispatch(char *line);

#endif  // COMMAND_H
