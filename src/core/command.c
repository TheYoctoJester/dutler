// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "core/command.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "core/outputs.h"
#include "core/settings.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "platform/bridge.h"
#include "platform/console.h"
#include "platform/debug.h"
#include "tusb.h"
#include "util/numparse.h"

// Stringify, with macro expansion (so DUTLER_XSTR(BRIDGE_BAUD_MAX) -> "4000000").
#define DUTLER_STR(x) #x
#define DUTLER_XSTR(x) DUTLER_STR(x)

extern bool g_boot_by_watchdog;  // defined in main.c

static bool dirty = false;  // unsaved settings changes

// ---------------------------------------------------------------------------
//  Command table — the SINGLE source of truth for the control-port vocabulary.
//  command_dispatch(), is_reserved_word() and the `help` listing all derive
//  from this, so adding/renaming a command can't leave any of them out of step.
// ---------------------------------------------------------------------------
typedef void (*cmd_fn)(char **sp);  // sp = strtok_r state positioned after the verb

typedef struct {
    const char *name;
    cmd_fn fn;
    const char *help;  // one line shown by `help`; NULL = hidden (e.g. aliases)
} command_t;

static void cmd_out(char **sp);
static void cmd_name(char **sp);
static void cmd_set(char **sp);
static void cmd_save(char **sp);
static void cmd_selftest(char **sp);
static void cmd_factory_reset(char **sp);
static void cmd_status(char **sp);
static void cmd_version(char **sp);
static void cmd_bootsel(char **sp);
static void cmd_help(char **sp);

// clang-format off
static const command_t commands[] = {
    {"out",           cmd_out,           "out <id> on|off|toggle  id=number or name"},
    {"name",          cmd_name,          "name <n> <alias|clear>  label output n"},
    {"set",           cmd_set,           "set baud <n> | set format <8N1>"},
    {"save",          cmd_save,          "save  persist names + bridge defaults"},
    {"selftest",      cmd_selftest,      "selftest  GP0<->GP1 loopback check"},
    {"factory-reset", cmd_factory_reset, "factory-reset confirm  erase saved settings"},
    {"status",        cmd_status,        "status  list outputs + bridge defaults"},
    {"version",       cmd_version,       "version  print firmware version"},
    {"bootsel",       cmd_bootsel,       "bootsel  reboot into USB bootloader"},
    {"reset",         cmd_bootsel,       NULL},  // hidden alias for bootsel
    {"help",          cmd_help,          "help  show this text"},
};
// clang-format on

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

// Command words that an output name must not shadow (they are matched first).
// Derived from the table, so it can never drift from the dispatcher.
static bool is_reserved_word(const char *w) {
    for (size_t i = 0; i < COMMAND_COUNT; i++)
        if (strcmp(w, commands[i].name) == 0) return true;
    return false;
}

// Perform an action on an already-resolved output. The action token is pulled
// from the caller's tokenizer state. Shared by "out <id> ..." and the bare
// "<name> ..." shorthand.
static void out_action(int idx, char **sp) {
    char *a_cmd = strtok_r(NULL, " \t", sp);
    if (!a_cmd) {
        console_print("error: usage '<output> on|off|toggle'\r\n");
        return;
    }

    if (strcmp(a_cmd, "on") == 0) {
        outputs_set(idx, true);
        dbg_printf("out %d -> on\r\n", idx + 1);
        console_print("ok\r\n");
    } else if (strcmp(a_cmd, "off") == 0) {
        outputs_set(idx, false);
        dbg_printf("out %d -> off\r\n", idx + 1);
        console_print("ok\r\n");
    } else if (strcmp(a_cmd, "toggle") == 0) {
        outputs_set(idx, !outputs_get(idx));
        dbg_printf("out %d -> %s (toggle)\r\n", idx + 1, outputs_get(idx) ? "on" : "off");
        console_print("ok\r\n");
    } else {
        console_print("error: unknown output command\r\n");
    }
}

static void cmd_out(char **sp) {
    char *a_id = strtok_r(NULL, " \t", sp);
    if (!a_id) {
        console_print("error: usage 'out <id> on|off|toggle'\r\n");
        return;
    }
    int idx = outputs_resolve(a_id);
    if (idx < 0) {
        console_print("error: unknown output\r\n");
        return;
    }
    out_action(idx, sp);
}

static void cmd_name(char **sp) {
    char *a_n = strtok_r(NULL, " \t", sp);
    char *a_alias = strtok_r(NULL, " \t", sp);
    if (!a_n || !a_alias) {
        console_print("error: usage 'name <n> <alias|clear>'\r\n");
        return;
    }
    uint32_t n;
    if (!parse_u32(a_n, &n) || n < 1 || n > OUT_COUNT) {
        console_print("error: output number out of range\r\n");
        return;
    }
    char *dst = g_settings.out_name[n - 1];
    if (strcmp(a_alias, "clear") == 0) {
        dst[0] = '\0';
    } else {
        uint32_t tmp;
        if (parse_u32(a_alias, &tmp)) {
            console_print("error: name cannot be all digits\r\n");
            return;
        }
        if (is_reserved_word(a_alias)) {
            console_print("error: name collides with a command word\r\n");
            return;
        }
        if (strlen(a_alias) >= OUT_NAME_MAX) {
            console_print("error: name too long\r\n");
            return;
        }
        strncpy(dst, a_alias, OUT_NAME_MAX - 1);
        dst[OUT_NAME_MAX - 1] = '\0';
    }
    dirty = true;
    console_print("ok\r\n");
}

static void cmd_set(char **sp) {
    char *what = strtok_r(NULL, " \t", sp);
    char *val = strtok_r(NULL, " \t", sp);
    if (!what || !val) {
        console_print("error: usage 'set baud <n>' | 'set format <8N1>'\r\n");
        return;
    }
    if (strcmp(what, "baud") == 0) {
        uint32_t b;
        if (!parse_u32(val, &b) || b < BRIDGE_BAUD_MIN || b > BRIDGE_BAUD_MAX) {
            console_print("error: baud out of range (" DUTLER_XSTR(
                BRIDGE_BAUD_MIN) ".." DUTLER_XSTR(BRIDGE_BAUD_MAX) ")\r\n");
            return;
        }
        g_settings.baud = b;
        dirty = true;
        console_print("ok (effective after 'save' + reboot)\r\n");
    } else if (strcmp(what, "format") == 0) {
        if (strlen(val) != 3) {
            console_print("error: format must be like 8N1\r\n");
            return;
        }
        int d = val[0] - '0';
        char p = val[1];
        int s = val[2] - '0';
        uint8_t parity;
        if (p == 'N' || p == 'n')
            parity = 0;
        else if (p == 'O' || p == 'o')
            parity = 1;
        else if (p == 'E' || p == 'e')
            parity = 2;
        else {
            console_print("error: parity must be N, O or E\r\n");
            return;
        }
        if (d < 5 || d > 8) {
            console_print("error: data bits must be 5..8\r\n");
            return;
        }
        if (s != 1 && s != 2) {
            console_print("error: stop bits must be 1 or 2\r\n");
            return;
        }
        g_settings.data_bits = (uint8_t)d;
        g_settings.parity = parity;
        g_settings.stop_bits = (uint8_t)s;
        dirty = true;
        console_print("ok (effective after 'save' + reboot)\r\n");
    } else {
        console_print("error: set what? (baud|format)\r\n");
    }
}

static void cmd_save(char **sp) {
    (void)sp;
    if (settings_save()) {
        dirty = false;
        console_print("saved\r\n");
    } else {
        console_print("error: save failed\r\n");
    }
}

static void cmd_status(char **sp) {
    (void)sp;
    char msg[64];
    for (uint8_t i = 0; i < OUT_COUNT; i++) {
        const char *nm = g_settings.out_name[i];
        const char *st = outputs_get(i) ? "on" : "off";
        if (nm[0])
            snprintf(msg, sizeof(msg), "out %u (%s) %s\r\n", (unsigned)(i + 1), nm, st);
        else
            snprintf(msg, sizeof(msg), "out %u %s\r\n", (unsigned)(i + 1), st);
        console_print(msg);
    }
    snprintf(msg, sizeof(msg), "bridge default " UART_MODE_FMT "\r\n",
             (unsigned long)g_settings.baud, g_settings.data_bits,
             parity_to_char(g_settings.parity), g_settings.stop_bits);
    console_print(msg);
    console_print("firmware DUTler " DUTLER_VERSION "\r\n");
    if (g_boot_by_watchdog) console_print("note: last reset was a watchdog timeout\r\n");
    if (dirty) console_print("(unsaved changes - use 'save')\r\n");
}

static void cmd_version(char **sp) {
    (void)sp;
    console_print("DUTler " DUTLER_VERSION "\r\n");
}

static void cmd_selftest(char **sp) {
    (void)sp;
    console_print(bridge_selftest() ? "selftest: GP0<->GP1 continuity OK\r\n"
                                    : "selftest: GP0<->GP1 OPEN (check the loopback jumper)\r\n");
}

static void cmd_factory_reset(char **sp) {
    char *a = strtok_r(NULL, " \t", sp);
    if (!a || strcmp(a, "confirm") != 0) {
        console_print("error: 'factory-reset confirm' erases all saved settings\r\n");
        return;
    }
    settings_reset();
    dirty = false;
    console_print("factory reset done (bridge UART defaults apply after reboot)\r\n");
}

static void cmd_bootsel(char **sp) {
    (void)sp;
    console_print("rebooting to BOOTSEL\r\n");
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (!time_reached(deadline)) tud_task();
    reset_usb_boot(0, 0);  // does not return
}

static void cmd_help(char **sp) {
    (void)sp;
    // Built from the table so it lists exactly the commands that exist. Kept
    // within one CDC TX FIFO (512 B) so the whole listing flushes in one go.
    console_print("DUTler control port\r\ncommands:\r\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (!commands[i].help) continue;  // hidden alias
        console_print("  ");
        console_print(commands[i].help);
        console_print("\r\n");
    }
    console_print("  <id> on|off|toggle      shorthand: drop the 'out' keyword\r\n");
}

void command_dispatch(char *s) {
    char *sp = NULL;
    char *tok = strtok_r(s, " \t", &sp);
    if (!tok) return;

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(tok, commands[i].name) == 0) {
            commands[i].fn(&sp);
            return;
        }
    }

    // Shorthand: an output number or configured name used as a verb,
    // e.g. "pump on" == "out pump on".
    int idx = outputs_resolve(tok);
    if (idx >= 0)
        out_action(idx, &sp);
    else
        console_print("error: unknown command (try 'help')\r\n");
}
