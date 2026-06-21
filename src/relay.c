#include "relay.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"

static const uint8_t relay_pins[RELAY_COUNT] = RELAY_PINS;
static bool relay_state[RELAY_COUNT];

#define LINE_MAX 64
static char line_buf[LINE_MAX];
static uint8_t line_len = 0;

static void cdc_print(const char *s) {
    tud_cdc_n_write_str(CDC_ITF_RELAY, s);
    tud_cdc_n_write_flush(CDC_ITF_RELAY);
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
        apply_relay(i, false);  // all OFF / safe state at power-on
    }
}

// Fired from a timer IRQ to release a pulsed relay.
static int64_t pulse_off_cb(alarm_id_t id, void *user_data) {
    (void)id;
    uint8_t idx = (uint8_t)(uintptr_t)user_data;
    if (idx < RELAY_COUNT) apply_relay(idx, false);
    return 0;  // one-shot
}

static void print_status(void) {
    char msg[40];
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        snprintf(msg, sizeof(msg), "relay %u %s\r\n", (unsigned)(i + 1),
                 relay_state[i] ? "on" : "off");
        cdc_print(msg);
    }
}

static void print_help(void) {
    cdc_print(
        "USB-UART-Relay control port\r\n"
        "commands (newline-terminated):\r\n"
        "  relay <n> on            energize relay n\r\n"
        "  relay <n> off           de-energize relay n\r\n"
        "  relay <n> toggle        flip relay n\r\n"
        "  relay <n> pulse <ms>    on for <ms> then auto-off\r\n"
        "  status                  list all relay states\r\n"
        "  bootsel                 reboot into USB bootloader (for reflashing)\r\n"
        "  help                    show this text\r\n");
}

static void parse_line(char *s) {
    char *tok = strtok(s, " \t");
    if (!tok) return;

    if (strcmp(tok, "help") == 0) {
        print_help();
        return;
    }
    if (strcmp(tok, "status") == 0) {
        print_status();
        return;
    }
    if (strcmp(tok, "bootsel") == 0 || strcmp(tok, "reset") == 0) {
        cdc_print("rebooting to BOOTSEL\r\n");
        // Let the reply flush over USB before we vanish into the bootloader.
        absolute_time_t deadline = make_timeout_time_ms(50);
        while (!time_reached(deadline)) tud_task();
        reset_usb_boot(0, 0);  // does not return
        return;
    }
    if (strcmp(tok, "relay") == 0) {
        char *a_num = strtok(NULL, " \t");
        char *a_cmd = strtok(NULL, " \t");
        if (!a_num || !a_cmd) {
            cdc_print("error: usage 'relay <n> on|off|toggle|pulse <ms>'\r\n");
            return;
        }
        int n = atoi(a_num);
        if (n < 1 || n > RELAY_COUNT) {
            cdc_print("error: relay number out of range\r\n");
            return;
        }
        uint8_t idx = (uint8_t)(n - 1);

        if (strcmp(a_cmd, "on") == 0) {
            apply_relay(idx, true);
            dbg_printf("relay %d -> on\r\n", n);
            cdc_print("ok\r\n");
        } else if (strcmp(a_cmd, "off") == 0) {
            apply_relay(idx, false);
            dbg_printf("relay %d -> off\r\n", n);
            cdc_print("ok\r\n");
        } else if (strcmp(a_cmd, "toggle") == 0) {
            apply_relay(idx, !relay_state[idx]);
            dbg_printf("relay %d -> %s (toggle)\r\n", n, relay_state[idx] ? "on" : "off");
            cdc_print("ok\r\n");
        } else if (strcmp(a_cmd, "pulse") == 0) {
            char *a_ms = strtok(NULL, " \t");
            int ms = a_ms ? atoi(a_ms) : 0;
            if (ms <= 0) {
                cdc_print("error: pulse needs ms > 0\r\n");
                return;
            }
            apply_relay(idx, true);
            add_alarm_in_ms(ms, pulse_off_cb, (void *)(uintptr_t)idx, true);
            dbg_printf("relay %d -> pulse %d ms\r\n", n, ms);
            cdc_print("ok\r\n");
        } else {
            cdc_print("error: unknown relay command\r\n");
        }
        return;
    }

    cdc_print("error: unknown command (try 'help')\r\n");
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
