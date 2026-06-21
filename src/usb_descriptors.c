// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>

#include "config.h"
#include "pico/unique_id.h"
#include "tusb.h"

// =====================================================================
//  USB descriptors for a composite device exposing TWO CDC-ACM ports.
//  CDC0 = transparent UART bridge, CDC1 = relay command channel.
// =====================================================================

// ---- Device descriptor ----
// Class set to "Misc / IAD" so the host treats the two CDC functions as
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

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

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

    // CDC 0 — UART bridge (interface string index 4)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, 4, EPNUM_CDC_0_NOTIF, 8, EPNUM_CDC_0_OUT,
                       EPNUM_CDC_0_IN, 64),

    // CDC 1 — relay control (interface string index 5)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, 5, EPNUM_CDC_1_NOTIF, 8, EPNUM_CDC_1_OUT,
                       EPNUM_CDC_1_IN, 64),

    // CDC 2 — debug log (interface string index 6)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_2, 6, EPNUM_CDC_2_NOTIF, 8, EPNUM_CDC_2_OUT,
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
    "Relay Control",             // 5: CDC1 interface
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

        const char *str = (index == 3) ? get_serial() : string_desc_arr[index];
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
