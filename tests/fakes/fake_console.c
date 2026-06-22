// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Captures everything command.c writes to the control port so tests can assert
// on the replies.
#include <string.h>

#include "fakes.h"
#include "platform/console.h"

#define CAP 4096
static char buf[CAP];
static size_t len;

void console_print(const char *s) {
    size_t n = strlen(s);
    if (len + n > CAP - 1) n = CAP - 1 - len;
    memcpy(buf + len, s, n);
    len += n;
    buf[len] = '\0';
}

const char *fake_console_text(void) { return buf; }

void fake_console_clear(void) {
    len = 0;
    buf[0] = '\0';
}
