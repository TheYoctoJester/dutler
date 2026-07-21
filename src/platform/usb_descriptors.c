// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "core/settings.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "platform/usb_descriptors.h"
#include "tusb.h"

// =====================================================================
//  USB descriptors for a composite device exposing THREE CDC-ACM ports.
//  CDC0 = transparent UART bridge, CDC1 = output command channel,
//  CDC2 = read-only firmware debug log.
// =====================================================================

// String-descriptor indices — the single source shared by the device descriptor
// (manufacturer/product/serial), the CDC interface descriptors, and
// tud_descriptor_string_cb(). string_desc_arr[] below is laid out in this order.
enum {
    STRID_LANGID = 0,    // supported-language list
    STRID_MANUFACTURER,  // 1
    STRID_PRODUCT,       // 2
    STRID_SERIAL,        // 3 — filled from the chip ID at runtime
    STRID_CDC0,          // 4 — "UART Bridge"
    STRID_CDC1,          // 5 — "Control"
    STRID_CDC2,          // 6 — "Debug Log"
};

// ---- Device descriptor ----
// Class set to "Misc / IAD" so the host treats the three CDC functions as
// one composite device.
static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,

    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,

    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

// ---- Configuration descriptor ----
enum {
    ITF_NUM_CDC_0 = 0,
    ITF_NUM_CDC_0_DATA,
    ITF_NUM_CDC_1,
    ITF_NUM_CDC_1_DATA,
    ITF_NUM_CDC_2,
    ITF_NUM_CDC_2_DATA,
    ITF_NUM_TOTAL,
};

// Endpoint addresses (must be unique; IN endpoints have the 0x80 bit set).
#define EPNUM_CDC_0_NOTIF 0x81
#define EPNUM_CDC_0_OUT 0x02
#define EPNUM_CDC_0_IN 0x82

#define EPNUM_CDC_1_NOTIF 0x83
#define EPNUM_CDC_1_OUT 0x04
#define EPNUM_CDC_1_IN 0x84

#define EPNUM_CDC_2_NOTIF 0x85
#define EPNUM_CDC_2_OUT 0x06
#define EPNUM_CDC_2_IN 0x86

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 3 * TUD_CDC_DESC_LEN)

static uint8_t const desc_configuration[] = {
    // config number, interface count, string index, total length, attribute, power (mA)
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // CDC 0 — UART bridge
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, STRID_CDC0, EPNUM_CDC_0_NOTIF, 8, EPNUM_CDC_0_OUT,
                       EPNUM_CDC_0_IN, 64),

    // CDC 1 — output control
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, STRID_CDC1, EPNUM_CDC_1_NOTIF, 8, EPNUM_CDC_1_OUT,
                       EPNUM_CDC_1_IN, 64),

    // CDC 2 — debug log
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_2, STRID_CDC2, EPNUM_CDC_2_NOTIF, 8, EPNUM_CDC_2_OUT,
                       EPNUM_CDC_2_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// ---- String descriptors ----
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: supported language = English (0x0409)
    "theyoctojester",            // 1: Manufacturer
    "DUTler",                    // 2: Product
    NULL,                        // 3: Serial number (filled from chip ID below)
    "UART Bridge",               // 4: CDC0 interface
    "Control",                   // 5: CDC1 interface
    "Debug Log",                 // 6: CDC2 interface
};

// Unique serial derived from the flash chip ID -> stable, distinct /dev nodes.
static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static const char *get_serial(void) {
    if (serial_str[0] == '\0') {
        pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
    }
    return serial_str;
}

const char *usb_get_serial(void) { return get_serial(); }

// Product string: "DUTler-<device_name>" when a name is set, else plain "DUTler".
// Keeps the greppable "DUTler" marker and lets the name show in /dev/serial/by-id
// while the immutable chip-ID stays as iSerial. "DUTler-" (7) + name (<=23) <= 31,
// the USB string-descriptor limit enforced below.
static char product_str[8 + DEVICE_NAME_MAX];

static const char *get_product(void) {
    if (g_settings.device_name[0] != '\0') {
        snprintf(product_str, sizeof(product_str), "DUTler-%s", g_settings.device_name);
        return product_str;
    }
    return "DUTler";
}

void usb_reenumerate(void) {
    // Flush any pending control-port reply, then bounce the link so the host
    // re-reads the (updated) product string. The 50 ms drain mirrors cmd_bootsel.
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (!time_reached(deadline)) tud_task();
    tud_disconnect();
    sleep_ms(100);
    tud_connect();
}

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }

        const char *str;
        if (index == STRID_SERIAL)
            str = get_serial();
        else if (index == STRID_PRODUCT)
            str = get_product();
        else
            str = string_desc_arr[index];
        if (str == NULL) return NULL;

        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // First word: descriptor type + total length.
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
