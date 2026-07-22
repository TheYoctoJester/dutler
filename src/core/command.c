// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "core/command.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "core/kvstore.h"
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
//  Response framing. Every command's reply ends with one status line so a
//  machine reader can stop without a timeout:
//    plain (shell-off) mode : `OK`            / `ERR <msg>`
//    interactive shell mode : `ok`/`ok (…)`   / `error: <msg>`   (colourised)
//  Data lines (if any) precede it. `detail` (reply_ok only) is a human hint
//  shown in shell mode and dropped in the terse machine protocol.
// ---------------------------------------------------------------------------
static void reply_ok(const char *detail) {
    if (g_settings.shell) {
        char line[96];
        if (detail && detail[0])
            snprintf(line, sizeof(line), "ok (%s)\r\n", detail);
        else
            snprintf(line, sizeof(line), "ok\r\n");
        console_print(line);
    } else {
        console_print("OK\r\n");
    }
}

static void reply_err(const char *msg) {
    char line[96];
    snprintf(line, sizeof(line), "%s %s\r\n", g_settings.shell ? "error:" : "ERR", msg);
    console_print(line);
}

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
    {"set",           cmd_set,           "set <key> <value>  configure a setting (keys below)"},
    {"get",           cmd_get,           "get [<key>]  read setting(s)/property(ies); no key = all"},
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
        reply_err("usage '<output> on|off|toggle'");
        return;
    }

    if (strcmp(a_cmd, "on") == 0) {
        outputs_set(idx, true);
        dbg_printf("out %d -> on\r\n", idx + 1);
        reply_ok(NULL);
    } else if (strcmp(a_cmd, "off") == 0) {
        outputs_set(idx, false);
        dbg_printf("out %d -> off\r\n", idx + 1);
        reply_ok(NULL);
    } else if (strcmp(a_cmd, "toggle") == 0) {
        outputs_set(idx, !outputs_get(idx));
        dbg_printf("out %d -> %s (toggle)\r\n", idx + 1, outputs_get(idx) ? "on" : "off");
        reply_ok(NULL);
    } else {
        reply_err("unknown output command");
    }
}

static void cmd_out(char **sp) {
    char *a_id = strtok_r(NULL, " \t", sp);
    if (!a_id) {
        reply_err("usage 'out <id> on|off|toggle'");
        return;
    }
    int idx = outputs_resolve(a_id);
    if (idx < 0) {
        reply_err("unknown output");
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
    console_drain();  // multi-line dumps can exceed one TX FIFO
    return true;
}

static void get_outname(uint8_t i) {
    char msg[64];
    snprintf(msg, sizeof(msg), "outname %u %s\r\n", (unsigned)(i + 1), g_settings.out_name[i]);
    console_print(msg);
    console_drain();
}

// Print one user KV entry; drain as we go — the list can exceed one TX FIFO.
static void print_kv(const char *k, const char *v) {
    char line[KV_KEY_MAX + KV_VALUE_MAX + 4];
    snprintf(line, sizeof(line), "%s %s\r\n", k, v);
    console_print(line);
    console_drain();
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
        reply_ok(NULL);
        return;
    }
    if (strcmp(key, "outname") == 0) {  // indexed: 'get outname' (all) or 'get outname <n>'
        char *a_n = strtok_r(NULL, " \t", sp);
        if (!a_n) {
            for (uint8_t i = 0; i < OUT_COUNT; i++) get_outname(i);
            reply_ok(NULL);
            return;
        }
        uint32_t n;
        if (!parse_u32(a_n, &n) || n < 1 || n > OUT_COUNT) {
            reply_err("output number out of range");
            return;
        }
        get_outname((uint8_t)(n - 1));
        reply_ok(NULL);
        return;
    }
    if (strcmp(key, "kv") == 0) {  // 'get kv' (all) or 'get kv <key>'
        char *sub = strtok_r(NULL, " \t", sp);
        if (!sub) {
            kv_foreach(print_kv);
            reply_ok(NULL);
            return;
        }
        const char *v = kv_get(sub);
        if (v) {
            char line[KV_KEY_MAX + KV_VALUE_MAX + 4];
            snprintf(line, sizeof(line), "%s %s\r\n", sub, v);
            console_print(line);
            reply_ok(NULL);
        } else {
            reply_err("no such key");
        }
        return;
    }
    if (get_scalar(key))
        reply_ok(NULL);
    else
        reply_err("unknown key (see 'help' for keys)");
}

static void cmd_set(char **sp) {
    char *what = strtok_r(NULL, " \t", sp);
    if (!what) {
        reply_err("usage 'set <key> <value>' (see 'help' for keys)");
        return;
    }

    // Indexed key: 'set outname <n> <alias|clear>' labels an output. The name is
    // usable as a shorthand verb, so it must not be all-digits or shadow a command.
    if (strcmp(what, "outname") == 0) {
        char *a_n = strtok_r(NULL, " \t", sp);
        char *a_alias = strtok_r(NULL, " \t", sp);
        if (!a_n || !a_alias) {
            reply_err("usage 'set outname <n> <alias|clear>'");
            return;
        }
        uint32_t n;
        if (!parse_u32(a_n, &n) || n < 1 || n > OUT_COUNT) {
            reply_err("output number out of range");
            return;
        }
        char *dst = g_settings.out_name[n - 1];
        if (strcmp(a_alias, "clear") == 0) {
            dst[0] = '\0';
        } else {
            uint32_t tmp;
            if (parse_u32(a_alias, &tmp)) {
                reply_err("name cannot be all digits");
                return;
            }
            if (is_reserved_word(a_alias)) {
                reply_err("name collides with a command word");
                return;
            }
            if (strlen(a_alias) >= OUT_NAME_MAX) {
                reply_err("name too long");
                return;
            }
            strncpy(dst, a_alias, OUT_NAME_MAX - 1);
            dst[OUT_NAME_MAX - 1] = '\0';
        }
        dirty = true;
        reply_ok(NULL);
        return;
    }

    // User key/value store: 'set kv <key> <value|clear>'. The value is the rest of
    // the line (spaces allowed), so it is parsed here, not as a single token.
    if (strcmp(what, "kv") == 0) {
        char *k = strtok_r(NULL, " \t", sp);
        char *rest = strtok_r(NULL, "", sp);  // remainder of the line (or NULL)
        if (!k) {
            reply_err("usage 'set kv <key> <value|clear>'");
            return;
        }
        if (strlen(k) >= KV_KEY_MAX) {
            reply_err("key too long");
            return;
        }
        for (const char *c = k; *c; c++) {
            bool ok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                      (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-';
            if (!ok) {
                reply_err("key may use only [A-Za-z0-9._-]");
                return;
            }
        }
        // A key named OK/ERR would make its 'get kv' line collide with the response
        // terminator; forbid it so the framed protocol stays unambiguous.
        if (strcmp(k, "OK") == 0 || strcmp(k, "ERR") == 0) {
            reply_err("key 'OK'/'ERR' is reserved");
            return;
        }
        char *v = rest;
        while (v && (*v == ' ' || *v == '\t')) v++;  // skip leading blanks
        if (v && strcmp(v, "clear") == 0) {
            kv_clear(k);  // clearing an absent key is a harmless no-op
            reply_ok("run 'save' to persist");
            return;
        }
        if (!v || !*v) {
            reply_err("usage 'set kv <key> <value|clear>'");
            return;
        }
        if (strlen(v) >= KV_VALUE_MAX) {
            reply_err("value too long");
            return;
        }
        if (!kv_set(k, v)) {
            reply_err("kv store full");
            return;
        }
        reply_ok("run 'save' to persist");
        return;
    }

    char *val = strtok_r(NULL, " \t", sp);
    if (!val) {
        reply_err("usage 'set <key> <value>'");
        return;
    }
    if (strcmp(what, "baud") == 0) {
        uint32_t b;
        if (!parse_u32(val, &b) || b < BRIDGE_BAUD_MIN || b > BRIDGE_BAUD_MAX) {
            reply_err("baud out of range (" DUTLER_XSTR(BRIDGE_BAUD_MIN) ".." DUTLER_XSTR(
                BRIDGE_BAUD_MAX) ")");
            return;
        }
        g_settings.baud = b;
        dirty = true;
        reply_ok("effective after 'save' + reboot");
    } else if (strcmp(what, "format") == 0) {
        if (strlen(val) != 3) {
            reply_err("format must be like 8N1");
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
            reply_err("parity must be N, O or E");
            return;
        }
        if (d < 5 || d > 8) {
            reply_err("data bits must be 5..8");
            return;
        }
        if (s != 1 && s != 2) {
            reply_err("stop bits must be 1 or 2");
            return;
        }
        g_settings.data_bits = (uint8_t)d;
        g_settings.parity = parity;
        g_settings.stop_bits = (uint8_t)s;
        dirty = true;
        reply_ok("effective after 'save' + reboot");
    } else if (strcmp(what, "echo") == 0) {
        // Control-port local echo. Unlike baud/format this takes effect at once
        // (console_task reads it live); 'save' just makes it stick across reboots.
        if (strcmp(val, "on") == 0)
            g_settings.echo = 1;
        else if (strcmp(val, "off") == 0)
            g_settings.echo = 0;
        else {
            reply_err("echo must be 'on' or 'off'");
            return;
        }
        dirty = true;
        reply_ok(NULL);
    } else if (strcmp(what, "shell") == 0) {
        // Interactive-shell mode: prompt, in-line editing, history (see lineedit.c).
        // Like echo it takes effect at once (console_task reads it live); 'save'
        // persists it. Off = the plain, scriptable line protocol.
        if (strcmp(val, "on") == 0)
            g_settings.shell = 1;
        else if (strcmp(val, "off") == 0)
            g_settings.shell = 0;
        else {
            reply_err("shell must be 'on' or 'off'");
            return;
        }
        dirty = true;
        reply_ok(NULL);
    } else if (strcmp(what, "dutname") == 0) {
        // Device/DUT label surfaced in the USB product string (and thus in
        // /dev/serial/by-id). Restrict to a udev-clean charset so the by-id path
        // is predictable. Applied live via a USB re-enumeration; 'save' persists.
        if (strcmp(val, "clear") == 0) {
            g_settings.device_name[0] = '\0';
        } else {
            if (strlen(val) >= DEVICE_NAME_MAX) {
                reply_err("name too long");
                return;
            }
            for (const char *c = val; *c; c++) {
                bool ok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                          (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-';
                if (!ok) {
                    reply_err("name may use only [A-Za-z0-9._-]");
                    return;
                }
            }
            strncpy(g_settings.device_name, val, DEVICE_NAME_MAX - 1);
            g_settings.device_name[DEVICE_NAME_MAX - 1] = '\0';
        }
        dirty = true;
        reply_ok("run 'save' to persist; re-enumerating USB");
        usb_reenumerate();  // drops open handles on this device; by-id path updates
    } else if (strcmp(what, "serial") == 0 || strcmp(what, "version") == 0) {
        reply_err("read-only property (use 'get')");
    } else {
        reply_err("unknown key (see 'help' for keys)");
    }
}

static void cmd_save(char **sp) {
    (void)sp;
    bool ok = settings_save();
    if (kv_dirty()) ok = kv_save() && ok;  // flush the user KV store too
    if (ok) {
        dirty = false;
        reply_ok(NULL);
    } else {
        reply_err("save failed");
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
        console_drain();  // status can exceed one TX FIFO
    }
    snprintf(msg, sizeof(msg), "bridge default " UART_MODE_FMT "\r\n",
             (unsigned long)g_settings.baud, g_settings.data_bits,
             parity_to_char(g_settings.parity), g_settings.stop_bits);
    console_print(msg);
    console_drain();
    console_print(g_settings.echo ? "echo on\r\n" : "echo off\r\n");
    console_print("firmware DUTler " DUTLER_VERSION "\r\n");
    if (g_boot_by_watchdog) console_print("note: last reset was a watchdog timeout\r\n");
    if (dirty || kv_dirty()) console_print("(unsaved changes - use 'save')\r\n");
    reply_ok(NULL);
}

static void cmd_selftest(char **sp) {
    (void)sp;
    // The jumper result is data; the command itself always succeeds -> OK.
    console_print(bridge_selftest() ? "selftest: GP0<->GP1 continuity OK\r\n"
                                    : "selftest: GP0<->GP1 OPEN (check the loopback jumper)\r\n");
    reply_ok(NULL);
}

static void cmd_factory_reset(char **sp) {
    char *a = strtok_r(NULL, " \t", sp);
    if (!a || strcmp(a, "confirm") != 0) {
        reply_err("'factory-reset confirm' erases all saved settings");
        return;
    }
    bool had_name = g_settings.device_name[0] != '\0';
    settings_reset();
    kv_reset();  // wipe the user key/value store too
    dirty = false;
    reply_ok("factory reset done; UART defaults apply after reboot");
    // The device name is part of the live USB identity; if one was set, bounce the
    // link so the by-id path drops back to the plain "DUTler" product string now.
    if (had_name) usb_reenumerate();
}

static void cmd_bootsel(char **sp) {
    (void)sp;
    reply_ok("rebooting to BOOTSEL");
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (!time_reached(deadline)) tud_task();
    reset_usb_boot(0, 0);  // does not return
}

static void cmd_reset(char **sp) {
    (void)sp;
    // A plain warm reboot into the application (unlike 'bootsel'), handy to clear
    // an occasional UART/USB lockup. watchdog_reboot() (rather than watchdog_enable)
    // means main.c does NOT flag the next boot as a watchdog timeout.
    reply_ok("rebooting");
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (!time_reached(deadline)) tud_task();  // flush the reply first
    watchdog_reboot(0, 0, 0);                    // fire ASAP; does not return on device
    while (!time_reached(make_timeout_time_ms(1000))) tud_task();  // wait for the reset
}

static void cmd_help(char **sp) {
    (void)sp;
    // Command list is built from the table so it lists exactly what exists. The
    // set/get keys are listed once in their own section rather than inline on the
    // set/get rows. The whole listing exceeds one CDC TX FIFO, so drain as we go.
    console_print("DUTler control port\r\ncommands:\r\n");
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (!commands[i].help) continue;  // hidden alias
        console_print("  ");
        console_print(commands[i].help);
        console_print("\r\n");
        console_drain();
    }
    console_print("  <id> on|off|toggle      shorthand: drop the 'out' keyword\r\n");
    console_print("get/set keys:\r\n");
    console_print("  baud, format, echo, shell, dutname, outname <n>  (read/write)\r\n");
    console_print("  serial, version  (read-only)\r\n");
    console_print(
        "  kv <key>  user key/value store (get kv lists all; set kv <key> clear deletes)\r\n");
    reply_ok(NULL);
    console_drain();
}

// Append entries of `list` that start with `prefix` to out[] (bounded by max).
static size_t add_matches(const char **out, size_t max, size_t n, const char *prefix,
                          const char *const *list, size_t nlist) {
    size_t pl = strlen(prefix);
    for (size_t i = 0; i < nlist && n < max; i++)
        if (strncmp(list[i], prefix, pl) == 0) out[n++] = list[i];
    return n;
}

// Append existing KV keys matching `prefix` to out[] (via kv_foreach). Uses file
// statics for the callback — command_complete is not reentrant.
static const char **g_kvc_out;
static size_t g_kvc_max, g_kvc_n, g_kvc_plen;
static const char *g_kvc_prefix;
static void kvc_collect(const char *k, const char *v) {
    (void)v;
    if (g_kvc_n < g_kvc_max && strncmp(k, g_kvc_prefix, g_kvc_plen) == 0) g_kvc_out[g_kvc_n++] = k;
}
static size_t add_kv_keys(const char **out, size_t max, size_t n, const char *prefix) {
    g_kvc_out = out;
    g_kvc_max = max;
    g_kvc_n = n;
    g_kvc_prefix = prefix;
    g_kvc_plen = strlen(prefix);
    kv_foreach(kvc_collect);
    return g_kvc_n;
}

size_t command_complete(const char *line, size_t cursor, const char **out, size_t max) {
    static const char *const set_keys[] = {"baud",    "format",  "echo", "shell",
                                           "dutname", "outname", "kv"};
    static const char *const get_keys[] = {"baud",    "format", "echo", "shell",  "dutname",
                                           "outname", "serial", "kv",   "version"};
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
            n = add_matches(out, max, n, prefix, on_off_tog,
                            sizeof(on_off_tog) / sizeof(*on_off_tog));
    } else if (tokpos == 2) {
        if (strcmp(argv[0], "set") == 0) {
            if (strcmp(argv[1], "echo") == 0 || strcmp(argv[1], "shell") == 0)
                n = add_matches(out, max, n, prefix, on_off, sizeof(on_off) / sizeof(*on_off));
            else if (strcmp(argv[1], "dutname") == 0)
                n = add_matches(out, max, n, prefix, clear_kw, 1);
            else if (strcmp(argv[1], "kv") == 0)  // 'set kv <keyprefix>' -> existing keys
                n = add_kv_keys(out, max, n, prefix);
        } else if (strcmp(argv[0], "get") == 0 && strcmp(argv[1], "kv") == 0) {
            n = add_kv_keys(out, max, n, prefix);  // 'get kv <keyprefix>'
        } else if (strcmp(argv[0], "out") == 0)
            n = add_matches(out, max, n, prefix, on_off_tog,
                            sizeof(on_off_tog) / sizeof(*on_off_tog));
    } else if (tokpos == 3) {
        if (strcmp(argv[0], "set") == 0 &&
            (strcmp(argv[1], "outname") == 0 || strcmp(argv[1], "kv") == 0))
            n = add_matches(out, max, n, prefix, clear_kw, 1);  // offer 'clear'
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
        reply_err("unknown command (try 'help')");
}
