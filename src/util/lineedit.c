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
//  Kill / yank (single kill buffer)
// ---------------------------------------------------------------------------

static void kill_set(lineedit_t *ed, const char *src, size_t n) {
    if (n > KILL_MAX - 1) n = KILL_MAX - 1;
    memcpy(ed->kill, src, n);
    ed->kill_len = n;
    ed->kill[n] = '\0';
}

// Remove buf[a..b) and fix up the cursor.
static void delete_range(lineedit_t *ed, size_t a, size_t b) {
    memmove(&ed->buf[a], &ed->buf[b], ed->len - b);
    ed->len -= (b - a);
    ed->buf[ed->len] = '\0';
    if (ed->pos >= b) ed->pos -= (b - a);
    else if (ed->pos > a) ed->pos = a;
}

static void kill_to_start(lineedit_t *ed) {  // Ctrl-U
    if (ed->pos == 0) return;
    kill_set(ed, ed->buf, ed->pos);
    delete_range(ed, 0, ed->pos);
    refresh(ed);
}

static void kill_to_end(lineedit_t *ed) {  // Ctrl-K
    if (ed->pos >= ed->len) return;
    kill_set(ed, ed->buf + ed->pos, ed->len - ed->pos);
    delete_range(ed, ed->pos, ed->len);
    refresh(ed);
}

static void kill_word(lineedit_t *ed) {  // Ctrl-W
    size_t i = ed->pos;
    while (i > 0 && ed->buf[i - 1] == ' ') i--;
    while (i > 0 && ed->buf[i - 1] != ' ') i--;
    if (i == ed->pos) return;
    kill_set(ed, ed->buf + i, ed->pos - i);
    delete_range(ed, i, ed->pos);
    refresh(ed);
}

static void yank(lineedit_t *ed) {  // Ctrl-Y
    if (ed->kill_len == 0) return;
    for (size_t i = 0; i < ed->kill_len; i++) insert_char(ed, ed->kill[i]);
}

// ---------------------------------------------------------------------------
//  Reverse-incremental history search (Ctrl-R)
// ---------------------------------------------------------------------------

// Most recent history entry at index >= from (1-based, older = larger) whose text
// contains the current query as a substring; 0 if none. Empty query matches all.
static size_t rsearch_find(lineedit_t *ed, size_t from) {
    for (size_t i = from; i <= ed->hist_count; i++)
        if (strstr(ed->hist[i - 1], ed->rquery)) return i;
    return 0;
}

static void rsearch_refresh(lineedit_t *ed) {
    ed->write("\r(reverse-i-search)`");
    ed->write(ed->rquery);
    ed->write("': ");
    ed->write(ed->rmatch ? ed->hist[ed->rmatch - 1] : "");
    ed->write("\x1b[K");
}

static void rsearch_enter(lineedit_t *ed) {
    strncpy(ed->stash, ed->buf, CONSOLE_LINE_MAX - 1);  // for restore on cancel
    ed->stash[CONSOLE_LINE_MAX - 1] = '\0';
    ed->rsearch = 1;
    ed->rquery_len = 0;
    ed->rquery[0] = '\0';
    ed->rmatch = rsearch_find(ed, 1);
    rsearch_refresh(ed);
}

// Leave search mode with `s` as the line, redrawn under the normal prompt.
static void rsearch_finish(lineedit_t *ed, const char *s) {
    strncpy(ed->buf, s, CONSOLE_LINE_MAX - 1);
    ed->buf[CONSOLE_LINE_MAX - 1] = '\0';
    ed->len = strlen(ed->buf);
    ed->pos = ed->len;
    ed->rsearch = 0;
    refresh(ed);
}

// ---------------------------------------------------------------------------
//  Completion (Tab)
// ---------------------------------------------------------------------------

#define LINEEDIT_MAX_COMPLETIONS 24

// Length of the longest common prefix across cand[0..n) (n >= 1).
static size_t common_prefix_len(const char *const *cand, size_t n) {
    for (size_t i = 0;; i++) {
        char c = cand[0][i];
        if (c == '\0') return i;
        for (size_t k = 1; k < n; k++)
            if (cand[k][i] != c) return i;
    }
}

// Insert a run of characters at the cursor (as if typed).
static void insert_run(lineedit_t *ed, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) insert_char(ed, s[i]);
}

static void do_complete(lineedit_t *ed) {
    if (!ed->complete) return;

    // Current token: from the last space before the cursor up to the cursor.
    size_t start = ed->pos;
    while (start > 0 && ed->buf[start - 1] != ' ') start--;
    size_t prefix_len = ed->pos - start;

    const char *cand[LINEEDIT_MAX_COMPLETIONS];
    size_t n = ed->complete(ed->buf, ed->pos, cand, LINEEDIT_MAX_COMPLETIONS);
    if (n == 0) return;

    if (n == 1) {  // unique: complete it and add a separating space
        insert_run(ed, cand[0] + prefix_len, strlen(cand[0]) - prefix_len);
        insert_char(ed, ' ');
        return;
    }

    // Several: extend by the longest common prefix if that makes progress,
    // otherwise list the candidates and redraw the prompt + line.
    size_t lcp = common_prefix_len(cand, n);
    if (lcp > prefix_len) {
        insert_run(ed, cand[0] + prefix_len, lcp - prefix_len);
        return;
    }
    ed->write("\r\n");
    for (size_t i = 0; i < n; i++) {
        ed->write(cand[i]);
        ed->write("  ");
    }
    ed->write("\r\n");
    refresh(ed);
}

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

    // --- reverse-incremental search sub-mode (Ctrl-R) ---
    if (ed->rsearch) {
        if (c == 0x12) {  // Ctrl-R again: next older match
            size_t m = rsearch_find(ed, ed->rmatch ? ed->rmatch + 1 : 1);
            if (m) ed->rmatch = m;
            rsearch_refresh(ed);
            return false;
        }
        if (c == '\r' || c == '\n') {  // accept + execute
            rsearch_finish(ed, ed->rmatch ? ed->hist[ed->rmatch - 1] : ed->stash);
            ed->write("\r\n");
            hist_push(ed, ed->buf);
            ed->hist_view = 0;
            *out_line = ed->buf;
            return true;
        }
        if (c == 0x07) {  // Ctrl-G: cancel, restore the pre-search line
            rsearch_finish(ed, ed->stash);
            return false;
        }
        if (c == 0x03) {  // Ctrl-C: cancel search and abort the line
            ed->rsearch = 0;
            ed->write("^C\r\n");
            ed->buf[0] = '\0';
            ed->len = 0;
            ed->pos = 0;
            ed->hist_view = 0;
            *out_line = ed->buf;
            return true;
        }
        if (c == 0x08 || c == 0x7f) {  // shorten the query
            if (ed->rquery_len > 0) ed->rquery[--ed->rquery_len] = '\0';
            ed->rmatch = rsearch_find(ed, 1);
            rsearch_refresh(ed);
            return false;
        }
        if (c >= 0x20 && c < 0x7f) {  // extend the query
            if (ed->rquery_len < sizeof(ed->rquery) - 1) {
                ed->rquery[ed->rquery_len++] = (char)c;
                ed->rquery[ed->rquery_len] = '\0';
            }
            ed->rmatch = rsearch_find(ed, 1);
            rsearch_refresh(ed);
            return false;
        }
        // Any other key accepts the match and is then processed normally.
        rsearch_finish(ed, ed->rmatch ? ed->hist[ed->rmatch - 1] : ed->stash);
        return lineedit_feed(ed, ch, out_line);
    }

    // --- normal byte ---
    switch (c) {
        case 0x1b:  // ESC — begin a sequence
            ed->esc = 1;
            return false;
        case 0x12:  // Ctrl-R — reverse history search
            rsearch_enter(ed);
            return false;
        case 0x15:  // Ctrl-U — kill to start of line
            kill_to_start(ed);
            return false;
        case 0x0b:  // Ctrl-K — kill to end of line
            kill_to_end(ed);
            return false;
        case 0x17:  // Ctrl-W — kill previous word
            kill_word(ed);
            return false;
        case 0x19:  // Ctrl-Y — yank the last kill
            yank(ed);
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
