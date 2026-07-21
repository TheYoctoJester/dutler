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
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "platform/bridge.h"
#include "platform/console.h"
#include "platform/debug.h"
#include "platform/usb_descriptors.h"
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
static void cmd_set(char **sp);
static void cmd_get(char **sp);
static void cmd_save(char **sp);
static void cmd_selftest(char **sp);
static void cmd_factory_reset(char **sp);
static void cmd_status(char **sp);
static void cmd_bootsel(char **sp);
static void cmd_reset(char **sp);
static void cmd_help(char **sp);

// clang-format off
static const command_t commands[] = {
    {"out",           cmd_out,           "out <id> on|off|toggle  id=number or name"},
    {"set",           cmd_set,           "set <key> <value>  keys: baud|format|echo|shell|dutname|outname <n>"},
    {"get",           cmd_get,           "get [<key>]  read setting(s): baud|format|echo|shell|dutname|outname <n>|serial|version"},
    {"save",          cmd_save,          "save  persist settings to flash"},
    {"selftest",      cmd_selftest,      "selftest  GP0<->GP1 loopback check"},
    {"factory-reset", cmd_factory_reset, "factory-reset confirm  erase saved settings"},
    {"status",        cmd_status,        "status  list outputs + bridge defaults"},
    {"bootsel",       cmd_bootsel,       "bootsel  reboot into USB bootloader"},
    {"reset",         cmd_reset,         "reset  reboot the board (into the application)"},
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

// --- get: settings + device properties exposed as a key/value store ---

// Print one scalar "key value" line. Returns false if the key is not a known
// scalar (the caller handles the indexed 'outname' and unknown-key reporting).
static bool get_scalar(const char *key) {
    char msg[64];
    if (strcmp(key, "baud") == 0) {
        snprintf(msg, sizeof(msg), "baud %lu\r\n", (unsigned long)g_settings.baud);
    } else if (strcmp(key, "format") == 0) {
        snprintf(msg, sizeof(msg), "format %u%c%u\r\n", (unsigned)g_settings.data_bits,
                 parity_to_char(g_settings.parity), (unsigned)g_settings.stop_bits);
    } else if (strcmp(key, "echo") == 0) {
        snprintf(msg, sizeof(msg), "echo %s\r\n", g_settings.echo ? "on" : "off");
    } else if (strcmp(key, "shell") == 0) {
        snprintf(msg, sizeof(msg), "shell %s\r\n", g_settings.shell ? "on" : "off");
    } else if (strcmp(key, "dutname") == 0) {
        snprintf(msg, sizeof(msg), "dutname %s\r\n", g_settings.device_name);
    } else if (strcmp(key, "serial") == 0) {
        snprintf(msg, sizeof(msg), "serial %s\r\n", usb_get_serial());
    } else if (strcmp(key, "version") == 0) {
        snprintf(msg, sizeof(msg), "version %s\r\n", DUTLER_VERSION);
    } else {
        return false;
    }
    console_print(msg);
    return true;
}

static void get_outname(uint8_t i) {
    char msg[64];
    snprintf(msg, sizeof(msg), "outname %u %s\r\n", (unsigned)(i + 1), g_settings.out_name[i]);
    console_print(msg);
}

static void cmd_get(char **sp) {
    char *key = strtok_r(NULL, " \t", sp);
    if (!key) {  // no key: dump the whole store
        get_scalar("baud");
        get_scalar("format");
        get_scalar("echo");
        get_scalar("shell");
        get_scalar("dutname");
        for (uint8_t i = 0; i < OUT_COUNT; i++) get_outname(i);
        get_scalar("serial");
        get_scalar("version");
        return;
    }
    if (strcmp(key, "outname") == 0) {  // indexed: 'get outname' (all) or 'get outname <n>'
        char *a_n = strtok_r(NULL, " \t", sp);
        if (!a_n) {
            for (uint8_t i = 0; i < OUT_COUNT; i++) get_outname(i);
            return;
        }
        uint32_t n;
        if (!parse_u32(a_n, &n) || n < 1 || n > OUT_COUNT) {
            console_print("error: output number out of range\r\n");
            return;
        }
        get_outname((uint8_t)(n - 1));
        return;
    }
    if (!get_scalar(key))
        console_print("error: unknown key (baud|format|echo|shell|dutname|outname|serial|version)\r\n");
}

static void cmd_set(char **sp) {
    char *what = strtok_r(NULL, " \t", sp);
    if (!what) {
        console_print(
            "error: usage 'set <key> <value>'  keys: baud|format|echo|shell|dutname|outname <n>\r\n");
        return;
    }

    // Indexed key: 'set outname <n> <alias|clear>' labels an output. The name is
    // usable as a shorthand verb, so it must not be all-digits or shadow a command.
    if (strcmp(what, "outname") == 0) {
        char *a_n = strtok_r(NULL, " \t", sp);
        char *a_alias = strtok_r(NULL, " \t", sp);
        if (!a_n || !a_alias) {
            console_print("error: usage 'set outname <n> <alias|clear>'\r\n");
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
        return;
    }

    char *val = strtok_r(NULL, " \t", sp);
    if (!val) {
        console_print("error: usage 'set <key> <value>'\r\n");
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
    } else if (strcmp(what, "echo") == 0) {
        // Control-port local echo. Unlike baud/format this takes effect at once
        // (console_task reads it live); 'save' just makes it stick across reboots.
        if (strcmp(val, "on") == 0)
            g_settings.echo = 1;
        else if (strcmp(val, "off") == 0)
            g_settings.echo = 0;
        else {
            console_print("error: echo must be 'on' or 'off'\r\n");
            return;
        }
        dirty = true;
        console_print("ok\r\n");
    } else if (strcmp(what, "shell") == 0) {
        // Interactive-shell mode: prompt, in-line editing, history (see lineedit.c).
        // Like echo it takes effect at once (console_task reads it live); 'save'
        // persists it. Off = the plain, scriptable line protocol.
        if (strcmp(val, "on") == 0)
            g_settings.shell = 1;
        else if (strcmp(val, "off") == 0)
            g_settings.shell = 0;
        else {
            console_print("error: shell must be 'on' or 'off'\r\n");
            return;
        }
        dirty = true;
        console_print("ok\r\n");
    } else if (strcmp(what, "dutname") == 0) {
        // Device/DUT label surfaced in the USB product string (and thus in
        // /dev/serial/by-id). Restrict to a udev-clean charset so the by-id path
        // is predictable. Applied live via a USB re-enumeration; 'save' persists.
        if (strcmp(val, "clear") == 0) {
            g_settings.device_name[0] = '\0';
        } else {
            if (strlen(val) >= DEVICE_NAME_MAX) {
                console_print("error: name too long\r\n");
                return;
            }
            for (const char *c = val; *c; c++) {
                bool ok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                          (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-';
                if (!ok) {
                    console_print("error: name may use only [A-Za-z0-9._-]\r\n");
                    return;
                }
            }
            strncpy(g_settings.device_name, val, DEVICE_NAME_MAX - 1);
            g_settings.device_name[DEVICE_NAME_MAX - 1] = '\0';
        }
        dirty = true;
        console_print("ok (run 'save' to persist); re-enumerating USB...\r\n");
        usb_reenumerate();  // drops open handles on this device; by-id path updates
    } else if (strcmp(what, "serial") == 0 || strcmp(what, "version") == 0) {
        console_print("error: read-only property (use 'get')\r\n");
    } else {
        console_print("error: unknown key (baud|format|echo|shell|dutname|outname)\r\n");
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
    console_print(g_settings.echo ? "echo on\r\n" : "echo off\r\n");
    console_print("firmware DUTler " DUTLER_VERSION "\r\n");
    if (g_boot_by_watchdog) console_print("note: last reset was a watchdog timeout\r\n");
    if (dirty) console_print("(unsaved changes - use 'save')\r\n");
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
    bool had_name = g_settings.device_name[0] != '\0';
    settings_reset();
    dirty = false;
    console_print("factory reset done (bridge UART defaults apply after reboot)\r\n");
    // The device name is part of the live USB identity; if one was set, bounce the
    // link so the by-id path drops back to the plain "DUTler" product string now.
    if (had_name) usb_reenumerate();
}

static void cmd_bootsel(char **sp) {
    (void)sp;
    console_print("rebooting to BOOTSEL\r\n");
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (!time_reached(deadline)) tud_task();
    reset_usb_boot(0, 0);  // does not return
}

static void cmd_reset(char **sp) {
    (void)sp;
    // A plain warm reboot into the application (unlike 'bootsel'), handy to clear
    // an occasional UART/USB lockup. watchdog_reboot() (rather than watchdog_enable)
    // means main.c does NOT flag the next boot as a watchdog timeout.
    console_print("rebooting\r\n");
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (!time_reached(deadline)) tud_task();  // flush the reply first
    watchdog_reboot(0, 0, 0);                     // fire ASAP; does not return on device
    while (!time_reached(make_timeout_time_ms(1000))) tud_task();  // wait for the reset
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

// Append entries of `list` that start with `prefix` to out[] (bounded by max).
static size_t add_matches(const char **out, size_t max, size_t n, const char *prefix,
                          const char *const *list, size_t nlist) {
    size_t pl = strlen(prefix);
    for (size_t i = 0; i < nlist && n < max; i++)
        if (strncmp(list[i], prefix, pl) == 0) out[n++] = list[i];
    return n;
}

size_t command_complete(const char *line, size_t cursor, const char **out, size_t max) {
    static const char *const set_keys[] = {"baud",  "format",  "echo",
                                           "shell", "dutname", "outname"};
    static const char *const get_keys[] = {"baud",    "format",  "echo",   "shell",
                                           "dutname", "outname", "serial", "version"};
    static const char *const on_off[] = {"on", "off"};
    static const char *const on_off_tog[] = {"on", "off", "toggle"};
    static const char *const clear_kw[] = {"clear"};

    if (max == 0) return 0;

    // Tokenize the line up to the cursor. A trailing space means we are starting a
    // fresh (empty-prefix) token; otherwise we are completing the last token.
    char tmp[CONSOLE_LINE_MAX];
    size_t clen = cursor < sizeof(tmp) - 1 ? cursor : sizeof(tmp) - 1;
    memcpy(tmp, line, clen);
    tmp[clen] = '\0';
    bool fresh = (clen == 0) || tmp[clen - 1] == ' ' || tmp[clen - 1] == '\t';

    char *argv[8];
    int argc = 0;
    char *sp = NULL;
    for (char *t = strtok_r(tmp, " \t", &sp); t && argc < 8; t = strtok_r(NULL, " \t", &sp))
        argv[argc++] = t;

    size_t tokpos = fresh ? (size_t)argc : (size_t)(argc - 1);
    const char *prefix = fresh ? "" : argv[argc - 1];
    size_t pl = strlen(prefix);
    size_t n = 0;

    if (tokpos == 0) {  // command verbs (not hidden) + output names (shorthand)
        for (size_t i = 0; i < COMMAND_COUNT && n < max; i++)
            if (commands[i].help && strncmp(commands[i].name, prefix, pl) == 0)
                out[n++] = commands[i].name;
        for (int i = 0; i < OUT_COUNT && n < max; i++)
            if (g_settings.out_name[i][0] && strncmp(g_settings.out_name[i], prefix, pl) == 0)
                out[n++] = g_settings.out_name[i];
    } else if (tokpos == 1) {
        if (strcmp(argv[0], "set") == 0)
            n = add_matches(out, max, n, prefix, set_keys, sizeof(set_keys) / sizeof(*set_keys));
        else if (strcmp(argv[0], "get") == 0)
            n = add_matches(out, max, n, prefix, get_keys, sizeof(get_keys) / sizeof(*get_keys));
        else if (strcmp(argv[0], "out") == 0) {
            for (int i = 0; i < OUT_COUNT && n < max; i++)
                if (g_settings.out_name[i][0] && strncmp(g_settings.out_name[i], prefix, pl) == 0)
                    out[n++] = g_settings.out_name[i];
        } else if (outputs_resolve(argv[0]) >= 0)  // "<output> ..." shorthand
            n = add_matches(out, max, n, prefix, on_off_tog, sizeof(on_off_tog) / sizeof(*on_off_tog));
    } else if (tokpos == 2) {
        if (strcmp(argv[0], "set") == 0) {
            if (strcmp(argv[1], "echo") == 0 || strcmp(argv[1], "shell") == 0)
                n = add_matches(out, max, n, prefix, on_off, sizeof(on_off) / sizeof(*on_off));
            else if (strcmp(argv[1], "dutname") == 0)
                n = add_matches(out, max, n, prefix, clear_kw, 1);
        } else if (strcmp(argv[0], "out") == 0)
            n = add_matches(out, max, n, prefix, on_off_tog, sizeof(on_off_tog) / sizeof(*on_off_tog));
    } else if (tokpos == 3) {
        if (strcmp(argv[0], "set") == 0 && strcmp(argv[1], "outname") == 0)
            n = add_matches(out, max, n, prefix, clear_kw, 1);
    }
    return n;
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
