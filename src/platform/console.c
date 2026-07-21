// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "platform/console.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "core/command.h"
#include "core/settings.h"
#include "tusb.h"
#include "util/lineedit.h"

// Plain (shell-off) line reader state. In shell-on mode the interactive editor
// below owns the line instead.
static char line_buf[CONSOLE_LINE_MAX];
static uint8_t line_len = 0;

// Interactive editor (shell-on mode). Lazily (re)initialised by console_task so
// enabling shell mode — or reconnecting — starts a fresh prompt.
static lineedit_t g_editor;
static bool editor_ready = false;

// True only while a command reply is being printed in shell mode: gates ANSI
// colour so the editor's own echo/redraw (which also goes through console_print)
// is never colourised.
static bool colorize_reply = false;

void console_print(const char *s) {
    // In shell mode, tint command replies: errors red, ok green. Heuristic on the
    // line prefix, so command handlers stay colour-agnostic.
    if (colorize_reply && g_settings.shell) {
        const char *color = NULL;
        if (strncmp(s, "error", 5) == 0) color = "\x1b[31m";       // red
        else if (strncmp(s, "ok", 2) == 0) color = "\x1b[32m";     // green
        if (color) {
            tud_cdc_n_write_str(CDC_ITF_OUT, color);
            tud_cdc_n_write_str(CDC_ITF_OUT, s);
            tud_cdc_n_write_str(CDC_ITF_OUT, "\x1b[0m");
            tud_cdc_n_write_flush(CDC_ITF_OUT);
            return;
        }
    }
    tud_cdc_n_write_str(CDC_ITF_OUT, s);
    tud_cdc_n_write_flush(CDC_ITF_OUT);
}

// Mirror input back to the terminal, but only when local echo is enabled
// (g_settings.echo). Handy for raw serial terminals that don't echo locally;
// off by default so scripted/automated drivers see a clean reply stream. Only
// used by the plain (shell-off) reader — the editor does its own echo.
static void echo(const char *s) {
    if (g_settings.echo) console_print(s);
}

// Host opened/closed the control port (DTR line). Greet on the rising edge.
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)rts;
    static bool was_open = false;
    if (itf != CDC_ITF_OUT) return;
    if (dtr && !was_open) {
        line_len = 0;         // discard any half-typed line from a previous session
        editor_ready = false;  // force a fresh editor init + prompt on reconnect
        console_print("\r\nDUTler control port. Type 'help' for commands.\r\n");
    }
    was_open = dtr;
}

// Interactive (shell-on) input: feed each byte to the editor, dispatch a
// finished line, then re-prompt.
static void shell_task(void) {
    if (!editor_ready) {
        lineedit_init(&g_editor, console_print, command_complete, SHELL_PROMPT);
        lineedit_start(&g_editor);
        editor_ready = true;
    }
    while (tud_cdc_n_available(CDC_ITF_OUT)) {
        int ch = tud_cdc_n_read_char(CDC_ITF_OUT);
        if (ch < 0) break;
        char *line;
        if (lineedit_feed(&g_editor, (char)ch, &line)) {
            if (line[0]) {
                colorize_reply = true;
                command_dispatch(line);
                colorize_reply = false;
            }
            lineedit_start(&g_editor);
        }
    }
}

// Plain (shell-off) input: assemble a line with backspace + optional echo, no
// prompt or escape handling — a clean, scriptable line protocol.
static void plain_task(void) {
    editor_ready = false;  // so turning shell back on re-inits + re-prompts
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
        } else if (line_len < CONSOLE_LINE_MAX - 1) {
            line_buf[line_len++] = (char)ch;
            char pair[2] = {(char)ch, '\0'};
            echo(pair);
        } else {
            line_len = 0;  // overrun: drop the line
            console_print("error: line too long\r\n");
        }
    }
}

void console_task(void) {
    if (g_settings.shell)
        shell_task();
    else
        plain_task();
}
