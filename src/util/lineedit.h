// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef DUTLER_LINEEDIT_H
#define DUTLER_LINEEDIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

// Interactive line editor for the control port (readline-style: prompt, in-line
// editing, history). Pure logic — all terminal I/O goes through the write
// callback — so it is unit-testable on the host without USB. console.c owns one
// instance and drives it byte-by-byte when shell mode is on.

// Byte-sink for editor output (echo, redraw, prompt); wired to console_print.
typedef void (*lineedit_write_fn)(const char *s);

// Completion provider: fill out[] with up to `max` candidate strings for the
// token at `cursor` in `line`; return the count. May be NULL (Tab does nothing).
typedef size_t (*lineedit_complete_fn)(const char *line, size_t cursor, const char **out,
                                       size_t max);

typedef struct {
    lineedit_write_fn write;
    lineedit_complete_fn complete;
    const char *prompt;

    char buf[CONSOLE_LINE_MAX];  // current line, always NUL-terminated at [len]
    size_t len;
    size_t pos;  // cursor, 0..len

    char hist[HISTORY_DEPTH][CONSOLE_LINE_MAX];  // newest at index 0
    size_t hist_count;                   // stored entries, <= HISTORY_DEPTH
    size_t hist_view;                    // 0 = live line; 1..hist_count = browsing
    char stash[CONSOLE_LINE_MAX];                // live line saved while browsing history

    uint8_t esc;       // input escape state: 0 none, 1 saw ESC, 2 in CSI (ESC[)
    uint32_t esc_num;  // accumulated CSI numeric parameter

    char kill[KILL_MAX];  // last kill (Ctrl-U/K/W), re-inserted by Ctrl-Y
    size_t kill_len;

    uint8_t rsearch;                // 1 = reverse-incremental-search sub-mode (Ctrl-R)
    char rquery[CONSOLE_LINE_MAX];  // current search query
    size_t rquery_len;
    size_t rmatch;                  // 1-based history index of the current match (0 = none)
} lineedit_t;

void lineedit_init(lineedit_t *ed, lineedit_write_fn write, lineedit_complete_fn complete,
                   const char *prompt);

// Begin a fresh input line: reset the buffer and print the prompt.
void lineedit_start(lineedit_t *ed);

// Feed one input byte. Returns true when a line is finished (Enter, or a Ctrl-C
// abort): *out_line then points at the NUL-terminated line inside the editor
// (empty string on an empty line or Ctrl-C). The caller dispatches a non-empty
// line, then calls lineedit_start() for the next prompt.
bool lineedit_feed(lineedit_t *ed, char ch, char **out_line);

#endif  // DUTLER_LINEEDIT_H
