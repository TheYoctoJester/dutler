#include "bridge.h"
#include "config.h"
#include "debug.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "relay.h"
#include "settings.h"
#include "tusb.h"

// True if this boot followed a watchdog timeout (a real hang), as opposed to a
// normal power-up or a deliberate bootsel/reflash. Read by the relay 'status'.
bool g_boot_by_watchdog = false;

int main(void) {
    // Capture the reset reason before anything else can disturb it.
    g_boot_by_watchdog = watchdog_enable_caused_reboot();

    settings_load();  // before bridge_init: provides the boot UART defaults
    tusb_init();
    bridge_init();
    relay_init();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Recover automatically if the main loop ever wedges (e.g. USB stack hang).
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true /*pause while debugging*/);

    dbg_printf("boot: %s\r\n", g_boot_by_watchdog ? "WATCHDOG RESET" : "normal");

    absolute_time_t next_blink = make_timeout_time_ms(500);
    bool led_on = false;

    while (true) {
        watchdog_update();  // "I'm still alive"

        tud_task();     // TinyUSB device stack
        bridge_task();  // CDC0 <-> UART
        relay_task();   // CDC1 command parser

        // Heartbeat so it's visibly alive even with no traffic.
        if (time_reached(next_blink)) {
            led_on = !led_on;
            gpio_put(LED_PIN, led_on);
            next_blink = make_timeout_time_ms(500);
        }

        // Sleep the core until something happens (USB or UART RX IRQ) or the
        // heartbeat is due, instead of spinning flat out. Worst-case wake is the
        // 500 ms heartbeat, well inside the watchdog timeout.
        best_effort_wfe_or_timeout(next_blink);
    }
}
