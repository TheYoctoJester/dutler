#include "bridge.h"
#include "config.h"
#include "pico/stdlib.h"
#include "relay.h"
#include "tusb.h"

int main(void) {
    tusb_init();
    bridge_init();
    relay_init();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    absolute_time_t next_blink = make_timeout_time_ms(500);
    bool led_on = false;

    while (true) {
        tud_task();     // TinyUSB device stack
        bridge_task();  // CDC0 <-> UART
        relay_task();   // CDC1 command parser

        // Heartbeat so it's visibly alive even with no traffic.
        if (time_reached(next_blink)) {
            led_on = !led_on;
            gpio_put(LED_PIN, led_on);
            next_blink = make_timeout_time_ms(500);
        }
    }
}
