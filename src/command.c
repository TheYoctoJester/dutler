// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "command.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bridge.h"
#include "config.h"
#include "console.h"
#include "outputs.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "settings.h"
#include "tusb.h"
#include "util/debug.h"
#include "util/numparse.h"

extern bool g_boot_by_watchdog;  // defined in main.c

static bool dirty = false;  // unsaved settings changes

static void print_status(void) {
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
    char pc = g_settings.parity == 1 ? 'O' : g_settings.parity == 2 ? 'E' : 'N';
    snprintf(msg, sizeof(msg), "bridge default %lu baud %u%c%u\r\n", (unsigned long)g_settings.baud,
             g_settings.data_bits, pc, g_settings.stop_bits);
    console_print(msg);
    console_print("firmware DUTler " DUTLER_VERSION "\r\n");
    if (g_boot_by_watchdog) console_print("note: last reset was a watchdog timeout\r\n");
    if (dirty) console_print("(unsaved changes - use 'save')\r\n");
}

static void print_help(void) {
    console_print(
        "DUTler control port\r\n"
        "commands (newline-terminated):\r\n"
        "  out <id> on|off|toggle      id = number 1.. or a name\r\n"
        "  <id> on|off|toggle          shorthand: drop the 'out' keyword\r\n"
        "  name <n> <alias|clear>      label output n\r\n"
        "  set baud <n>                bridge boot baud rate\r\n"
        "  set format <8N1>            bridge boot data/parity/stop\r\n"
        "  save                        persist names + bridge defaults\r\n"
        "  selftest                    GP0<->GP1 loopback continuity check\r\n"
        "  factory-reset confirm       erase saved settings (back to defaults)\r\n"
        "  status                      list outputs + bridge defaults\r\n"
        "  version                     print firmware version\r\n"
        "  bootsel                     reboot into USB bootloader\r\n"
        "  help                        show this text\r\n");
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

// Command words that an output name must not shadow (they are matched first).
static bool is_reserved_word(const char *w) {
    static const char *const reserved[] = {"out",  "name",    "set",   "save",          "status",
                                           "help", "bootsel", "reset", "factory-reset", "version"};
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
        if (strcmp(w, reserved[i]) == 0) return true;
    return false;
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
        if (!parse_u32(val, &b) || b < 50 || b > 4000000) {
            console_print("error: baud out of range (50..4000000)\r\n");
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

static void cmd_save(void) {
    if (settings_save()) {
        dirty = false;
        console_print("saved\r\n");
    } else {
        console_print("error: save failed\r\n");
    }
}

void command_dispatch(char *s) {
    char *sp = NULL;
    char *tok = strtok_r(s, " \t", &sp);
    if (!tok) return;

    if (strcmp(tok, "help") == 0) {
        print_help();
    } else if (strcmp(tok, "status") == 0) {
        print_status();
    } else if (strcmp(tok, "out") == 0) {
        cmd_out(&sp);
    } else if (strcmp(tok, "name") == 0) {
        cmd_name(&sp);
    } else if (strcmp(tok, "set") == 0) {
        cmd_set(&sp);
    } else if (strcmp(tok, "save") == 0) {
        cmd_save();
    } else if (strcmp(tok, "version") == 0) {
        console_print("DUTler " DUTLER_VERSION "\r\n");
    } else if (strcmp(tok, "selftest") == 0) {
        console_print(bridge_selftest()
                          ? "selftest: GP0<->GP1 continuity OK\r\n"
                          : "selftest: GP0<->GP1 OPEN (check the loopback jumper)\r\n");
    } else if (strcmp(tok, "factory-reset") == 0) {
        char *a = strtok_r(NULL, " \t", &sp);
        if (!a || strcmp(a, "confirm") != 0) {
            console_print("error: 'factory-reset confirm' erases all saved settings\r\n");
        } else {
            settings_reset();
            dirty = false;
            console_print("factory reset done (bridge UART defaults apply after reboot)\r\n");
        }
    } else if (strcmp(tok, "bootsel") == 0 || strcmp(tok, "reset") == 0) {
        console_print("rebooting to BOOTSEL\r\n");
        absolute_time_t deadline = make_timeout_time_ms(50);
        while (!time_reached(deadline)) tud_task();
        reset_usb_boot(0, 0);  // does not return
    } else {
        // Shorthand: an output number or configured name used as a verb,
        // e.g. "pump on" == "out pump on".
        int idx = outputs_resolve(tok);
        if (idx >= 0)
            out_action(idx, &sp);
        else
            console_print("error: unknown command (try 'help')\r\n");
    }
}
