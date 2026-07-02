// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "platform/console.h"

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "core/command.h"
#include "core/settings.h"
#include "tusb.h"

#define LINE_MAX 80
static char line_buf[LINE_MAX];
static uint8_t line_len = 0;

void console_print(const char *s) {
    tud_cdc_n_write_str(CDC_ITF_OUT, s);
    tud_cdc_n_write_flush(CDC_ITF_OUT);
}

// Mirror input back to the terminal, but only when local echo is enabled
// (g_settings.echo). Handy for raw serial terminals that don't echo locally;
// off by default so scripted/automated drivers see a clean reply stream.
static void echo(const char *s) {
    if (g_settings.echo) console_print(s);
}

// Host opened/closed the control port (DTR line). Greet on the rising edge.
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)rts;
    static bool was_open = false;
    if (itf != CDC_ITF_OUT) return;
    if (dtr && !was_open) {
        line_len = 0;  // discard any half-typed line from a previous session
        console_print("\r\nDUTler control port. Type 'help' for commands.\r\n");
    }
    was_open = dtr;
}

void console_task(void) {
    while (tud_cdc_n_available(CDC_ITF_OUT)) {
        int ch = tud_cdc_n_read_char(CDC_ITF_OUT);
        if (ch < 0) break;

        if (ch == '\r' || ch == '\n') {
            if (line_len > 0) {
                echo("\r\n");  // close the echoed input line before the reply
                line_buf[line_len] = '\0';
                command_dispatch(line_buf);
                line_len = 0;
            }
        } else if (ch == '\b' || ch == 0x7f) {  // backspace / DEL: rub out one char
            if (line_len > 0) {
                line_len--;
                echo("\b \b");  // erase the glyph on the terminal
            }
        } else if (line_len < LINE_MAX - 1) {
            line_buf[line_len++] = (char)ch;
            char pair[2] = {(char)ch, '\0'};
            echo(pair);
        } else {
            line_len = 0;  // overrun: drop the line
            console_print("error: line too long\r\n");
        }
    }
}
