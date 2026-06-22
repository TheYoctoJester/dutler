// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include "config.h"
#include "core/outputs.h"
#include "core/settings.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "platform/bridge.h"
#include "platform/console.h"
#include "platform/debug.h"
#include "tusb.h"

// The main loop sleeps with best_effort_wfe_or_timeout(), which relies on the
// default alarm pool to schedule the bounded wake that keeps the loop turning
// (and thus the watchdog fed) when the USB bus is idle. Make that dependency a
// build error rather than a silent 3 a.m. reset if the pool is ever disabled.
#if PICO_TIME_DEFAULT_ALARM_POOL_DISABLED
#error "main loop needs the default alarm pool to bound its sleep / feed the watchdog"
#endif

// True if this boot followed a watchdog timeout (a real hang), as opposed to a
// normal power-up or a deliberate bootsel/reflash. Reported by the 'status' command.
bool g_boot_by_watchdog = false;

int main(void) {
    // Capture the reset reason before anything else can disturb it.
    g_boot_by_watchdog = watchdog_enable_caused_reboot();

    settings_load();  // before bridge_init: provides the boot UART defaults
    tusb_init();
    bridge_init();
    outputs_init();

#if DUTLER_HAVE_LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
#endif

    // Recover automatically if the main loop ever wedges (e.g. USB stack hang).
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true /*pause while debugging*/);

    dbg_printf("DUTler %s — boot: %s\r\n", DUTLER_VERSION,
               g_boot_by_watchdog ? "WATCHDOG RESET" : "normal");

    // The blink deadline also bounds the loop's sleep (keeps the watchdog fed),
    // so it runs on every board even where there is no LED to toggle.
    absolute_time_t next_blink = make_timeout_time_ms(HEARTBEAT_MS);
#if DUTLER_HAVE_LED
    bool led_on = false;
#endif

    while (true) {
        watchdog_update();  // "I'm still alive"

        tud_task();      // TinyUSB device stack
        bridge_task();   // CDC0 <-> UART
        console_task();  // CDC1 control port -> command interpreter

        // Heartbeat so it's visibly alive even with no traffic.
        if (time_reached(next_blink)) {
#if DUTLER_HAVE_LED
            led_on = !led_on;
            gpio_put(LED_PIN, led_on);
#endif
            next_blink = make_timeout_time_ms(HEARTBEAT_MS);
        }

        // Sleep the core until something happens (USB or UART RX IRQ) or the
        // heartbeat is due, instead of spinning flat out. The watchdog is fed
        // from this loop on purpose (so a wedged loop is caught); the heartbeat
        // deadline guarantees the loop turns over within HEARTBEAT_MS — well inside
        // WATCHDOG_TIMEOUT_MS — via the default alarm pool (asserted present above).
        best_effort_wfe_or_timeout(next_blink);
    }
}
