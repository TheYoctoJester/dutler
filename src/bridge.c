#include "bridge.h"

#include "config.h"
#include "debug.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "settings.h"
#include "tusb.h"

static uart_parity_t parity_enum(uint8_t p) {
    return p == 1 ? UART_PARITY_ODD : p == 2 ? UART_PARITY_EVEN : UART_PARITY_NONE;
}

// Ring buffer filled by the UART RX IRQ, drained to USB in bridge_task().
#define RX_RING_SIZE 1024
static uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head = 0;  // written by IRQ
static volatile uint16_t rx_tail = 0;  // written by task

static void on_uart_rx(void) {
    while (uart_is_readable(BRIDGE_UART)) {
        uint8_t c = (uint8_t)uart_getc(BRIDGE_UART);
        uint16_t next = (uint16_t)((rx_head + 1) % RX_RING_SIZE);
        if (next != rx_tail) {
            rx_ring[rx_head] = c;
            rx_head = next;
        }
        // Ring full: drop the byte (host isn't draining fast enough).
    }
}

void bridge_init(void) {
    uart_init(BRIDGE_UART, g_settings.baud);
    gpio_set_function(BRIDGE_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(BRIDGE_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(BRIDGE_UART, false, false);
    uart_set_format(BRIDGE_UART, g_settings.data_bits, g_settings.stop_bits,
                    parity_enum(g_settings.parity));
    uart_set_fifo_enabled(BRIDGE_UART, true);

    irq_set_exclusive_handler(BRIDGE_UART_IRQ, on_uart_rx);
    irq_set_enabled(BRIDGE_UART_IRQ, true);
    uart_set_irq_enables(BRIDGE_UART, true, false);  // RX only
}

void bridge_task(void) {
    // USB (CDC0) -> UART
    if (tud_cdc_n_available(CDC_ITF_BRIDGE)) {
        uint8_t buf[64];
        uint32_t n = tud_cdc_n_read(CDC_ITF_BRIDGE, buf, sizeof(buf));
        if (n) uart_write_blocking(BRIDGE_UART, buf, n);
    }

    // UART -> USB (CDC0)
    while (rx_tail != rx_head && tud_cdc_n_write_available(CDC_ITF_BRIDGE) > 0) {
        uint8_t c = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1) % RX_RING_SIZE);
        tud_cdc_n_write_char(CDC_ITF_BRIDGE, (char)c);
    }
    tud_cdc_n_write_flush(CDC_ITF_BRIDGE);
}

// Host opened/reconfigured the bridge port -> mirror onto the hardware UART,
// making this behave like a real USB-serial adapter (FT232/CP2102-style).
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
    // Standard "1200-baud touch" reset, applied to the debug port only. The
    // bridge port must stay free to use any real baud rate (incl. 1200).
    if (itf == CDC_ITF_DEBUG && coding->bit_rate == 1200) {
        reset_usb_boot(0, 0);  // does not return
        return;
    }

    if (itf != CDC_ITF_BRIDGE) return;

    uint data_bits = coding->data_bits;
    if (data_bits < 5) data_bits = 5;
    if (data_bits > 8) data_bits = 8;

    uint stop_bits = (coding->stop_bits == CDC_LINE_CODING_STOP_BITS_2) ? 2 : 1;

    uart_parity_t parity = UART_PARITY_NONE;
    if (coding->parity == CDC_LINE_CODING_PARITY_ODD) parity = UART_PARITY_ODD;
    if (coding->parity == CDC_LINE_CODING_PARITY_EVEN) parity = UART_PARITY_EVEN;

    if (coding->bit_rate) uart_set_baudrate(BRIDGE_UART, coding->bit_rate);
    uart_set_format(BRIDGE_UART, data_bits, stop_bits, parity);

    char pc = (parity == UART_PARITY_ODD) ? 'O' : (parity == UART_PARITY_EVEN) ? 'E' : 'N';
    dbg_printf("bridge: line coding %lu baud %u%c%u\r\n",
               (unsigned long)coding->bit_rate, data_bits, pc, stop_bits);
}
