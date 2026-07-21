// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Host stand-ins for the handful of Pico SDK / TinyUSB calls reachable from
// command.c and outputs.c. See tests/shims/ for the matching headers.
#include <stdbool.h>

#include "fakes.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "platform/usb_descriptors.h"
#include "tusb.h"

// Normally defined in main.c (true after a watchdog reset); status reads it.
bool g_boot_by_watchdog = false;

// --- GPIO (outputs.c) : no-ops; output state is tracked inside outputs.c ---
void gpio_init(uint gpio) { (void)gpio; }
void gpio_set_dir(uint gpio, bool out) {
    (void)gpio;
    (void)out;
}
void gpio_put(uint gpio, bool value) {
    (void)gpio;
    (void)value;
}

// --- bootrom : record the request instead of rebooting ---
static bool bootsel_requested;
void reset_usb_boot(uint32_t usb_activity_gpio_pin_mask, uint32_t disable_interface_mask) {
    (void)usb_activity_gpio_pin_mask;
    (void)disable_interface_mask;
    bootsel_requested = true;
}
bool fake_bootsel_requested(void) { return bootsel_requested; }
void fake_bootsel_clear(void) { bootsel_requested = false; }

// --- watchdog : record a warm-reboot request instead of rebooting ---
static int reboot_count;
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc;
    (void)sp;
    (void)delay_ms;
    reboot_count++;
}
int fake_reboot_count(void) { return reboot_count; }
void fake_reboot_clear(void) { reboot_count = 0; }

// --- TinyUSB / time : make the bootsel countdown fall through immediately ---
void tud_task(void) {}
absolute_time_t make_timeout_time_ms(uint32_t ms) {
    (void)ms;
    return 0;
}
bool time_reached(absolute_time_t t) {
    (void)t;
    return true;
}

// --- usb_descriptors seam (command.c: `serial`, `set name`) ---
const char *usb_get_serial(void) { return "TESTSERIAL000001"; }

static int reenumerate_count;
void usb_reenumerate(void) { reenumerate_count++; }
int fake_reenumerate_count(void) { return reenumerate_count; }
void fake_reenumerate_clear(void) { reenumerate_count = 0; }
