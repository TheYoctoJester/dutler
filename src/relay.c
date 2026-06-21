#include "relay.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "settings.h"
#include "tusb.h"

static const uint8_t relay_pins[RELAY_COUNT] = RELAY_PINS;
static bool relay_state[RELAY_COUNT];
static bool dirty = false;  // unsaved settings changes

#define LINE_MAX 80
static char line_buf[LINE_MAX];
static uint8_t line_len = 0;

static void cdc_print(const char *s) {
    tud_cdc_n_write_str(CDC_ITF_RELAY, s);
    tud_cdc_n_write_flush(CDC_ITF_RELAY);
}

// Parse a base-10 unsigned integer, rejecting empty input and trailing junk
// (unlike atoi(), which silently treats "banana" / "12x" as 0).
static bool parse_u32(const char *s, uint32_t *out) {
    if (!s || !*s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*end != '\0') return false;  // reject anything not fully numeric
    *out = (uint32_t)v;
    return true;
}

static void apply_relay(uint8_t idx, bool on) {
    relay_state[idx] = on;
    bool level = RELAY_ACTIVE_LOW ? !on : on;
    gpio_put(relay_pins[idx], level);
}

void relay_init(void) {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        gpio_init(relay_pins[i]);
        gpio_set_dir(relay_pins[i], GPIO_OUT);
        apply_relay(i, false);  // always OFF at power-on (safe state)
    }
}

// Fired from a timer IRQ to release a pulsed relay.
static int64_t pulse_off_cb(alarm_id_t id, void *user_data) {
    (void)id;
    uint8_t idx = (uint8_t)(uintptr_t)user_data;
    if (idx < RELAY_COUNT) apply_relay(idx, false);
    return 0;  // one-shot
}

// Resolve a relay reference: a 1-based number or a configured name.
static int resolve_relay(const char *tok) {
    if (!tok || !tok[0]) return -1;
    uint32_t n;
    if (parse_u32(tok, &n))
        return (n >= 1 && n <= RELAY_COUNT) ? (int)(n - 1) : -1;
    for (int i = 0; i < RELAY_COUNT; i++)
        if (g_settings.relay_name[i][0] &&
            strcmp(g_settings.relay_name[i], tok) == 0)
            return i;
    return -1;
}

static void print_status(void) {
    char msg[64];
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        const char *nm = g_settings.relay_name[i];
        const char *st = relay_state[i] ? "on" : "off";
        if (nm[0])
            snprintf(msg, sizeof(msg), "relay %u (%s) %s\r\n", i + 1, nm, st);
        else
            snprintf(msg, sizeof(msg), "relay %u %s\r\n", i + 1, st);
        cdc_print(msg);
    }
    char pc = g_settings.parity == 1 ? 'O' : g_settings.parity == 2 ? 'E' : 'N';
    snprintf(msg, sizeof(msg), "bridge default %lu baud %u%c%u\r\n",
             (unsigned long)g_settings.baud, g_settings.data_bits, pc,
             g_settings.stop_bits);
    cdc_print(msg);
    if (dirty) cdc_print("(unsaved changes - use 'save')\r\n");
}

static void print_banner(void) {
    cdc_print("\r\nUSB-UART-Relay control port. Type 'help' for commands.\r\n");
}

// Host opened/closed the relay port (DTR line). Greet on the rising edge.
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)rts;
    static bool was_open = false;
    if (itf != CDC_ITF_RELAY) return;
    if (dtr && !was_open) {
        line_len = 0;  // discard any half-typed line from a previous session
        print_banner();
    }
    was_open = dtr;
}

static void print_help(void) {
    cdc_print(
        "USB-UART-Relay control port\r\n"
        "commands (newline-terminated):\r\n"
        "  relay <id> on|off|toggle    id = number 1.. or a name\r\n"
        "  relay <id> pulse <ms>       on for <ms> then auto-off\r\n"
        "  <id> on|off|toggle|pulse    shorthand: drop the 'relay' keyword\r\n"
        "  name <n> <alias|clear>      label relay n\r\n"
        "  set baud <n>                bridge boot baud rate\r\n"
        "  set format <8N1>            bridge boot data/parity/stop\r\n"
        "  save                        persist names + bridge defaults\r\n"
        "  factory-reset confirm       erase saved settings (back to defaults)\r\n"
        "  status                      list relays + bridge defaults\r\n"
        "  bootsel                     reboot into USB bootloader\r\n"
        "  help                        show this text\r\n");
}

// Perform an action on an already-resolved relay. The remaining tokens (the
// action and, for pulse, its argument) are pulled from the caller's tokenizer
// state. Shared by "relay <id> ..." and the bare "<name> ..." shorthand.
static void relay_action(int idx, char **sp) {
    char *a_cmd = strtok_r(NULL, " \t", sp);
    if (!a_cmd) {
        cdc_print("error: usage '<relay> on|off|toggle|pulse <ms>'\r\n");
        return;
    }

    if (strcmp(a_cmd, "on") == 0) {
        apply_relay(idx, true);
        dbg_printf("relay %d -> on\r\n", idx + 1);
        cdc_print("ok\r\n");
    } else if (strcmp(a_cmd, "off") == 0) {
        apply_relay(idx, false);
        dbg_printf("relay %d -> off\r\n", idx + 1);
        cdc_print("ok\r\n");
    } else if (strcmp(a_cmd, "toggle") == 0) {
        apply_relay(idx, !relay_state[idx]);
        dbg_printf("relay %d -> %s (toggle)\r\n", idx + 1,
                   relay_state[idx] ? "on" : "off");
        cdc_print("ok\r\n");
    } else if (strcmp(a_cmd, "pulse") == 0) {
        char *a_ms = strtok_r(NULL, " \t", sp);
        uint32_t ms;
        if (!a_ms || !parse_u32(a_ms, &ms) || ms == 0) {
            cdc_print("error: pulse needs ms > 0\r\n");
            return;
        }
        apply_relay(idx, true);
        add_alarm_in_ms(ms, pulse_off_cb, (void *)(uintptr_t)idx, true);
        dbg_printf("relay %d -> pulse %lu ms\r\n", idx + 1, (unsigned long)ms);
        cdc_print("ok\r\n");
    } else {
        cdc_print("error: unknown relay command\r\n");
    }
}

static void cmd_relay(char **sp) {
    char *a_id = strtok_r(NULL, " \t", sp);
    if (!a_id) {
        cdc_print("error: usage 'relay <id> on|off|toggle|pulse <ms>'\r\n");
        return;
    }
    int idx = resolve_relay(a_id);
    if (idx < 0) {
        cdc_print("error: unknown relay\r\n");
        return;
    }
    relay_action(idx, sp);
}

// Command words that a relay name must not shadow (they are matched first).
static bool is_reserved_word(const char *w) {
    static const char *const reserved[] = {
        "relay", "name",    "set",   "save", "status",
        "help",  "bootsel", "reset", "factory-reset"};
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
        if (strcmp(w, reserved[i]) == 0) return true;
    return false;
}

static void cmd_name(char **sp) {
    char *a_n = strtok_r(NULL, " \t", sp);
    char *a_alias = strtok_r(NULL, " \t", sp);
    if (!a_n || !a_alias) {
        cdc_print("error: usage 'name <n> <alias|clear>'\r\n");
        return;
    }
    uint32_t n;
    if (!parse_u32(a_n, &n) || n < 1 || n > RELAY_COUNT) {
        cdc_print("error: relay number out of range\r\n");
        return;
    }
    char *dst = g_settings.relay_name[n - 1];
    if (strcmp(a_alias, "clear") == 0) {
        dst[0] = '\0';
    } else {
        uint32_t tmp;
        if (parse_u32(a_alias, &tmp)) {
            cdc_print("error: name cannot be all digits\r\n");
            return;
        }
        if (is_reserved_word(a_alias)) {
            cdc_print("error: name collides with a command word\r\n");
            return;
        }
        if (strlen(a_alias) >= RELAY_NAME_MAX) {
            cdc_print("error: name too long\r\n");
            return;
        }
        strncpy(dst, a_alias, RELAY_NAME_MAX - 1);
        dst[RELAY_NAME_MAX - 1] = '\0';
    }
    dirty = true;
    cdc_print("ok\r\n");
}

static void cmd_set(char **sp) {
    char *what = strtok_r(NULL, " \t", sp);
    char *val = strtok_r(NULL, " \t", sp);
    if (!what || !val) {
        cdc_print("error: usage 'set baud <n>' | 'set format <8N1>'\r\n");
        return;
    }
    if (strcmp(what, "baud") == 0) {
        uint32_t b;
        if (!parse_u32(val, &b) || b < 50 || b > 4000000) {
            cdc_print("error: baud out of range (50..4000000)\r\n");
            return;
        }
        g_settings.baud = b;
        dirty = true;
        cdc_print("ok (effective after 'save' + reboot)\r\n");
    } else if (strcmp(what, "format") == 0) {
        if (strlen(val) != 3) {
            cdc_print("error: format must be like 8N1\r\n");
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
            cdc_print("error: parity must be N, O or E\r\n");
            return;
        }
        if (d < 5 || d > 8) {
            cdc_print("error: data bits must be 5..8\r\n");
            return;
        }
        if (s != 1 && s != 2) {
            cdc_print("error: stop bits must be 1 or 2\r\n");
            return;
        }
        g_settings.data_bits = (uint8_t)d;
        g_settings.parity = parity;
        g_settings.stop_bits = (uint8_t)s;
        dirty = true;
        cdc_print("ok (effective after 'save' + reboot)\r\n");
    } else {
        cdc_print("error: set what? (baud|format)\r\n");
    }
}

static void cmd_save(void) {
    if (settings_save()) {
        dirty = false;
        cdc_print("saved\r\n");
    } else {
        cdc_print("error: save failed\r\n");
    }
}

static void parse_line(char *s) {
    char *sp = NULL;
    char *tok = strtok_r(s, " \t", &sp);
    if (!tok) return;

    if (strcmp(tok, "help") == 0) {
        print_help();
    } else if (strcmp(tok, "status") == 0) {
        print_status();
    } else if (strcmp(tok, "relay") == 0) {
        cmd_relay(&sp);
    } else if (strcmp(tok, "name") == 0) {
        cmd_name(&sp);
    } else if (strcmp(tok, "set") == 0) {
        cmd_set(&sp);
    } else if (strcmp(tok, "save") == 0) {
        cmd_save();
    } else if (strcmp(tok, "factory-reset") == 0) {
        char *a = strtok_r(NULL, " \t", &sp);
        if (!a || strcmp(a, "confirm") != 0) {
            cdc_print("error: 'factory-reset confirm' erases all saved settings\r\n");
        } else {
            settings_reset();
            dirty = false;
            cdc_print("factory reset done (bridge UART defaults apply after reboot)\r\n");
        }
    } else if (strcmp(tok, "bootsel") == 0 || strcmp(tok, "reset") == 0) {
        cdc_print("rebooting to BOOTSEL\r\n");
        absolute_time_t deadline = make_timeout_time_ms(50);
        while (!time_reached(deadline)) tud_task();
        reset_usb_boot(0, 0);  // does not return
    } else {
        // Shorthand: a relay number or configured name used as a verb,
        // e.g. "pump on" == "relay pump on".
        int idx = resolve_relay(tok);
        if (idx >= 0)
            relay_action(idx, &sp);
        else
            cdc_print("error: unknown command (try 'help')\r\n");
    }
}

void relay_task(void) {
    while (tud_cdc_n_available(CDC_ITF_RELAY)) {
        int ch = tud_cdc_n_read_char(CDC_ITF_RELAY);
        if (ch < 0) break;

        if (ch == '\r' || ch == '\n') {
            if (line_len > 0) {
                line_buf[line_len] = '\0';
                parse_line(line_buf);
                line_len = 0;
            }
        } else if (line_len < LINE_MAX - 1) {
            line_buf[line_len++] = (char)ch;
        } else {
            line_len = 0;  // overrun: drop the line
            cdc_print("error: line too long\r\n");
        }
    }
}
