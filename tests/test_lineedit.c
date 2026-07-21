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
    return UNITY_END();
}
