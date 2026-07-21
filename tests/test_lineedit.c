// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the interactive line editor (lineedit.c). The editor is pure:
// all terminal output goes through a write callback, which these tests capture
// into a buffer, and input is fed one byte at a time (including ANSI escape
// sequences for the arrow / Home / End / Delete keys).

#include <string.h>

#include "unity.h"
#include "util/lineedit.h"

// --- captured editor output ---
static char cap[4096];
static size_t cap_len;
static void cap_write(const char *s) {
    size_t n = strlen(s);
    if (cap_len + n < sizeof(cap)) {
        memcpy(cap + cap_len, s, n);
        cap_len += n;
        cap[cap_len] = '\0';
    }
}
static void cap_clear(void) {
    cap_len = 0;
    cap[0] = '\0';
}

// Escape sequences for the keys the editor understands.
#define K_UP "\x1b[A"
#define K_DOWN "\x1b[B"
#define K_RIGHT "\x1b[C"
#define K_LEFT "\x1b[D"
#define K_HOME "\x1b[H"
#define K_END "\x1b[F"
#define K_DEL "\x1b[3~"

static lineedit_t ed;

void setUp(void) {
    cap_clear();
    lineedit_init(&ed, cap_write, NULL, "> ");
    lineedit_start(&ed);
    cap_clear();  // drop the initial prompt so tests assert on their own output
}
void tearDown(void) {}

// Feed a byte string; return the last completed line (NULL if none finished).
// Mirrors console.c: after a finished line, copy it out and re-start the editor
// (which resets the buffer for the next line), so multi-command feeds behave.
static char last_line[CONSOLE_LINE_MAX];
static char *feed(const char *bytes) {
    bool got = false;
    for (const char *p = bytes; *p; p++) {
        char *out = NULL;
        if (lineedit_feed(&ed, *p, &out)) {
            strncpy(last_line, out, sizeof(last_line) - 1);
            last_line[sizeof(last_line) - 1] = '\0';
            got = true;
            lineedit_start(&ed);
        }
    }
    return got ? last_line : NULL;
}

static void test_basic_line(void) {
    char *line = feed("get baud\r");
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("get baud", line);
}

static void test_empty_enter(void) {
    char *line = feed("\r");
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("", line);  // caller skips dispatch for an empty line
}

static void test_midline_insert(void) {
    // "helo", move left once (before 'o'), insert 'l' -> "hello".
    char *line = feed("helo" K_LEFT "l\r");
    TEST_ASSERT_EQUAL_STRING("hello", line);
}

static void test_backspace_and_delete(void) {
    // Backspace at end.
    TEST_ASSERT_EQUAL_STRING("hell", feed("hello\x7f\r"));
    // Delete under cursor: type "xhello", Home, Delete -> "hello".
    TEST_ASSERT_EQUAL_STRING("hello", feed("xhello" K_HOME K_DEL "\r"));
}

static void test_home_end(void) {
    // Home then insert prepends; End then insert appends.
    TEST_ASSERT_EQUAL_STRING("Zabc", feed("abc" K_HOME "Z\r"));
    TEST_ASSERT_EQUAL_STRING("abcY", feed("abc" K_END "Y\r"));
    // Ctrl-A / Ctrl-E equivalents.
    TEST_ASSERT_EQUAL_STRING("Qabc", feed("abc\x01Q\r"));  // Ctrl-A
    TEST_ASSERT_EQUAL_STRING("abcW", feed("abc\x05W\r"));  // Ctrl-E
}

static void test_history_recall(void) {
    feed("one\r");
    feed("two\r");
    // Up -> most recent ("two"), Up -> ("one"), Down -> ("two"), Enter.
    TEST_ASSERT_EQUAL_STRING("two", feed(K_UP K_UP K_DOWN "\r"));
}

static void test_history_dedup_and_stash(void) {
    feed("first\r");
    feed("same\r");
    feed("same\r");  // consecutive dup: not stored twice
    // Up -> "same", Up -> "first" (proves only one "same" entry exists).
    TEST_ASSERT_EQUAL_STRING("first", feed(K_UP K_UP "\r"));

    // Live line is stashed while browsing and restored coming back down.
    char *line = feed("draft" K_UP K_DOWN "\r");
    TEST_ASSERT_EQUAL_STRING("draft", line);
}

static void test_ctrl_c_aborts(void) {
    char *line = feed("junk\x03");  // Ctrl-C
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("", line);  // aborted -> empty, nothing dispatched
    TEST_ASSERT_NOT_NULL(strstr(cap, "^C"));
}

static void test_prompt_on_start(void) {
    cap_clear();
    lineedit_start(&ed);
    TEST_ASSERT_NOT_NULL(strstr(cap, "> "));  // prompt was emitted
}

// --- Tab completion (driven by a configurable stub provider) ---
static const char **g_cands;
static size_t g_ncands;
static size_t stub_complete(const char *line, size_t cursor, const char **out, size_t max) {
    size_t start = cursor;
    while (start > 0 && line[start - 1] != ' ') start--;
    size_t pl = cursor - start;
    const char *pre = line + start;
    size_t n = 0;
    for (size_t i = 0; i < g_ncands && n < max; i++)
        if (strncmp(g_cands[i], pre, pl) == 0) out[n++] = g_cands[i];
    return n;
}
static void init_stub(void) {
    lineedit_init(&ed, cap_write, stub_complete, "> ");
    lineedit_start(&ed);
    cap_clear();
}

static void test_tab_unique(void) {
    static const char *C[] = {"baud", "format", "echo"};
    g_cands = C;
    g_ncands = 3;
    init_stub();
    // "for" + Tab -> only "format" matches -> completes with a trailing space.
    TEST_ASSERT_EQUAL_STRING("format ", feed("for\t\r"));
}

static void test_tab_common_prefix(void) {
    static const char *C[] = {"setbaud", "setecho"};
    g_cands = C;
    g_ncands = 2;
    init_stub();
    // "s" + Tab -> two matches sharing "set" -> extend to the common prefix only.
    TEST_ASSERT_EQUAL_STRING("set", feed("s\t\r"));
}

static void test_tab_list(void) {
    static const char *C[] = {"baud", "format", "echo"};
    g_cands = C;
    g_ncands = 3;
    init_stub();
    // Empty prefix + Tab -> no common prefix -> list all candidates.
    cap_clear();
    feed("\t");
    TEST_ASSERT_NOT_NULL(strstr(cap, "baud"));
    TEST_ASSERT_NOT_NULL(strstr(cap, "format"));
    TEST_ASSERT_NOT_NULL(strstr(cap, "echo"));
    TEST_ASSERT_EQUAL_STRING("", feed("\r"));  // buffer stayed empty
}

static void test_kill_yank(void) {
    // Ctrl-U kills to start; Ctrl-Y yanks it back.
    TEST_ASSERT_EQUAL_STRING("hello world", feed("hello world\x15\x19\r"));
    // Ctrl-A to start, Ctrl-K kills to end; Ctrl-Y restores.
    TEST_ASSERT_EQUAL_STRING("hello world", feed("hello world\x01\x0b\x19\r"));
    // Ctrl-W kills the previous word (leaving the trailing space).
    TEST_ASSERT_EQUAL_STRING("foo bar ", feed("foo bar baz\x17\r"));
}

// Reset to a known 3-entry history (newest last). Accepting a match re-pushes it,
// so each search scenario starts from a clean, predictable history.
static void load_hist3(void) {
    lineedit_init(&ed, cap_write, NULL, "> ");
    lineedit_start(&ed);
    feed("get baud\r");
    feed("set echo on\r");
    feed("get version\r");
}

static void test_reverse_search(void) {
    // Ctrl-R + "baud" finds "get baud"; Enter executes it.
    load_hist3();
    TEST_ASSERT_EQUAL_STRING("get baud", feed("\x12" "baud\r"));
    // Ctrl-R "get" -> newest "get version"; Ctrl-R again -> older "get baud".
    load_hist3();
    TEST_ASSERT_EQUAL_STRING("get baud", feed("\x12" "get\x12\r"));
    // Ctrl-G cancels and restores the pre-search line.
    load_hist3();
    TEST_ASSERT_EQUAL_STRING("draft", feed("draft\x12" "echo\x07\r"));
}

static void test_wrap(void) {
    // Force a narrow terminal so lines span multiple rows; the buffer must stay
    // correct through typing and mid-line editing regardless of wrapping.
    lineedit_init(&ed, cap_write, NULL, "> ");
    ed.cols = 10;
    ed.width_queried = 1;  // skip the ESC[6n probe
    lineedit_start(&ed);
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrst", feed("abcdefghijklmnopqrst\r"));
    // Ctrl-A to the start of a wrapped line, then insert.
    TEST_ASSERT_EQUAL_STRING("Xabcdefghijklmnop", feed("abcdefghijklmnop\x01X\r"));
}

static void test_crlf_coalesce(void) {
    char *o;
    // CRLF must yield exactly one completed line, not two.
    lineedit_init(&ed, cap_write, NULL, "> ");
    ed.width_queried = 1;
    lineedit_start(&ed);
    int lines = 0;
    for (const char *p = "hi\r\n"; *p; p++)
        if (lineedit_feed(&ed, *p, &o)) lines++;
    TEST_ASSERT_EQUAL_INT(1, lines);
    // LFCR too.
    lines = 0;
    for (const char *p = "yo\n\r"; *p; p++)
        if (lineedit_feed(&ed, *p, &o)) lines++;
    TEST_ASSERT_EQUAL_INT(1, lines);
    // Two real Enters (bare CRs) are still two lines.
    lines = 0;
    for (const char *p = "a\rb\r"; *p; p++)
        if (lineedit_feed(&ed, *p, &o)) lines++;
    TEST_ASSERT_EQUAL_INT(2, lines);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_basic_line);
    RUN_TEST(test_empty_enter);
    RUN_TEST(test_midline_insert);
    RUN_TEST(test_backspace_and_delete);
    RUN_TEST(test_home_end);
    RUN_TEST(test_history_recall);
    RUN_TEST(test_history_dedup_and_stash);
    RUN_TEST(test_ctrl_c_aborts);
    RUN_TEST(test_prompt_on_start);
    RUN_TEST(test_tab_unique);
    RUN_TEST(test_tab_common_prefix);
    RUN_TEST(test_tab_list);
    RUN_TEST(test_kill_yank);
    RUN_TEST(test_reverse_search);
    RUN_TEST(test_wrap);
    RUN_TEST(test_crlf_coalesce);
    return UNITY_END();
}
