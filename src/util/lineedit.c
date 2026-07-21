// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "util/lineedit.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
//  Terminal output helpers
// ---------------------------------------------------------------------------

// Redraw the whole line in place: return to column 0, print prompt + buffer,
// clear any leftover from a previously longer line, then reposition the cursor.
// Single-row refresh (assumes prompt+line fits the terminal width — wrap-aware
// refresh is a later concern). Used for edits that move text; plain appends and
// cursor moves emit minimal sequences directly.
static void refresh(lineedit_t *ed) {
    ed->write("\r");
    ed->write(ed->prompt);
    ed->write(ed->buf);      // NUL-terminated at [len]
    ed->write("\x1b[K");     // erase to end of line
    size_t back = ed->len - ed->pos;
    if (back > 0) {
        char seq[16];
        snprintf(seq, sizeof(seq), "\x1b[%uD", (unsigned)back);
        ed->write(seq);
    }
}

// ---------------------------------------------------------------------------
//  Buffer edits (all keep buf[len] == '\0')
// ---------------------------------------------------------------------------

static void insert_char(lineedit_t *ed, char c) {
    if (ed->len + 1 >= CONSOLE_LINE_MAX) return;  // full (keep room for the NUL)
    if (ed->pos == ed->len) {
        // Fast path: appending at the end needs only the one glyph echoed.
        ed->buf[ed->pos++] = c;
        ed->len++;
        ed->buf[ed->len] = '\0';
        char pair[2] = {c, '\0'};
        ed->write(pair);
        return;
    }
    memmove(&ed->buf[ed->pos + 1], &ed->buf[ed->pos], ed->len - ed->pos);
    ed->buf[ed->pos++] = c;
    ed->len++;
    ed->buf[ed->len] = '\0';
    refresh(ed);
}

static void backspace(lineedit_t *ed) {
    if (ed->pos == 0) return;
    memmove(&ed->buf[ed->pos - 1], &ed->buf[ed->pos], ed->len - ed->pos);
    ed->pos--;
    ed->len--;
    ed->buf[ed->len] = '\0';
    refresh(ed);
}

static void delete_at(lineedit_t *ed) {
    if (ed->pos >= ed->len) return;
    memmove(&ed->buf[ed->pos], &ed->buf[ed->pos + 1], ed->len - ed->pos - 1);
    ed->len--;
    ed->buf[ed->len] = '\0';
    refresh(ed);
}

static void move_left(lineedit_t *ed) {
    if (ed->pos > 0) {
        ed->pos--;
        ed->write("\x1b[D");
    }
}

static void move_right(lineedit_t *ed) {
    if (ed->pos < ed->len) {
        ed->pos++;
        ed->write("\x1b[C");
    }
}

static void move_home(lineedit_t *ed) {
    ed->pos = 0;
    refresh(ed);
}

static void move_end(lineedit_t *ed) {
    ed->pos = ed->len;
    refresh(ed);
}

// Replace the whole line with `s` and redraw (used by history recall).
static void set_line(lineedit_t *ed, const char *s) {
    strncpy(ed->buf, s, CONSOLE_LINE_MAX - 1);
    ed->buf[CONSOLE_LINE_MAX - 1] = '\0';
    ed->len = strlen(ed->buf);
    ed->pos = ed->len;
    refresh(ed);
}

// ---------------------------------------------------------------------------
//  History (ring, newest at index 0)
// ---------------------------------------------------------------------------

static void hist_push(lineedit_t *ed, const char *s) {
    if (s[0] == '\0') return;
    if (ed->hist_count > 0 && strcmp(ed->hist[0], s) == 0) return;  // no consecutive dup
    size_t last = (ed->hist_count < HISTORY_DEPTH) ? ed->hist_count : HISTORY_DEPTH - 1;
    for (size_t i = last; i > 0; i--) memcpy(ed->hist[i], ed->hist[i - 1], CONSOLE_LINE_MAX);
    strncpy(ed->hist[0], s, CONSOLE_LINE_MAX - 1);
    ed->hist[0][CONSOLE_LINE_MAX - 1] = '\0';
    if (ed->hist_count < HISTORY_DEPTH) ed->hist_count++;
}

static void hist_up(lineedit_t *ed) {  // older
    if (ed->hist_view >= ed->hist_count) return;
    if (ed->hist_view == 0) {  // stash the live line before leaving it
        strncpy(ed->stash, ed->buf, CONSOLE_LINE_MAX - 1);
        ed->stash[CONSOLE_LINE_MAX - 1] = '\0';
    }
    ed->hist_view++;
    set_line(ed, ed->hist[ed->hist_view - 1]);
}

static void hist_down(lineedit_t *ed) {  // newer
    if (ed->hist_view == 0) return;
    ed->hist_view--;
    set_line(ed, ed->hist_view == 0 ? ed->stash : ed->hist[ed->hist_view - 1]);
}

// ---------------------------------------------------------------------------
//  Completion (Tab) — wired in a later commit; no-op without a provider
// ---------------------------------------------------------------------------

static void do_complete(lineedit_t *ed) { (void)ed; }

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

void lineedit_init(lineedit_t *ed, lineedit_write_fn write, lineedit_complete_fn complete,
                   const char *prompt) {
    memset(ed, 0, sizeof(*ed));
    ed->write = write;
    ed->complete = complete;
    ed->prompt = prompt;
    ed->buf[0] = '\0';
}

void lineedit_start(lineedit_t *ed) {
    ed->len = 0;
    ed->pos = 0;
    ed->buf[0] = '\0';
    ed->hist_view = 0;
    ed->esc = 0;
    ed->write(ed->prompt);
}

bool lineedit_feed(lineedit_t *ed, char ch, char **out_line) {
    unsigned char c = (unsigned char)ch;

    // --- escape-sequence state machine (arrow / Home / End / Delete keys) ---
    if (ed->esc == 1) {  // saw ESC
        if (c == '[') {
            ed->esc = 2;
            ed->esc_num = 0;
        } else {
            ed->esc = 0;  // lone ESC or unsupported prefix — ignore
        }
        return false;
    }
    if (ed->esc == 2) {  // inside CSI (ESC[)
        if (c >= '0' && c <= '9') {
            ed->esc_num = ed->esc_num * 10u + (uint32_t)(c - '0');
            return false;
        }
        ed->esc = 0;  // this byte is the final one
        switch (c) {
            case 'A': hist_up(ed); break;
            case 'B': hist_down(ed); break;
            case 'C': move_right(ed); break;
            case 'D': move_left(ed); break;
            case 'H': move_home(ed); break;
            case 'F': move_end(ed); break;
            case '~':
                if (ed->esc_num == 3) delete_at(ed);
                else if (ed->esc_num == 1 || ed->esc_num == 7) move_home(ed);
                else if (ed->esc_num == 4 || ed->esc_num == 8) move_end(ed);
                break;
            default: break;  // ignore anything else
        }
        return false;
    }

    // --- normal byte ---
    switch (c) {
        case 0x1b:  // ESC — begin a sequence
            ed->esc = 1;
            return false;
        case '\r':
        case '\n':  // Enter — finish the line
            ed->write("\r\n");
            ed->buf[ed->len] = '\0';
            hist_push(ed, ed->buf);
            ed->hist_view = 0;
            *out_line = ed->buf;
            return true;
        case 0x03:  // Ctrl-C — abort the current line
            ed->write("^C\r\n");
            ed->buf[0] = '\0';
            ed->len = 0;
            ed->pos = 0;
            ed->hist_view = 0;
            *out_line = ed->buf;
            return true;
        case 0x08:
        case 0x7f:  // Backspace / DEL
            backspace(ed);
            return false;
        case 0x01:  // Ctrl-A — start of line
            move_home(ed);
            return false;
        case 0x05:  // Ctrl-E — end of line
            move_end(ed);
            return false;
        case '\t':  // Tab — completion
            do_complete(ed);
            return false;
        default:
            if (c >= 0x20 && c < 0x7f) insert_char(ed, (char)c);  // printable
            return false;                                          // ignore other controls
    }
}
