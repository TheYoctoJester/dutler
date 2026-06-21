// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Host unit tests for DUTler's pure logic: CRC-32, integer parsing, and the
// settings record codec. No Pico SDK — compiled with the native toolchain.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "parse.h"
#include "settings_codec.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        checks++;                                                  \
        if (!(cond)) {                                             \
            failures++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static void test_crc32(void) {
    // Standard CRC-32 check value for "123456789".
    CHECK(dutler_crc32("123456789", 9) == 0xCBF43926u);
    CHECK(dutler_crc32("", 0) == 0u);  // init ^ xorout
    CHECK(dutler_crc32("hello", 5) != dutler_crc32("hellp", 5));
}

static void test_parse_u32(void) {
    uint32_t v = 123;
    CHECK(parse_u32("0", &v) && v == 0);
    CHECK(parse_u32("115200", &v) && v == 115200u);
    CHECK(parse_u32("4294967295", &v) && v == 4294967295u);  // UINT32_MAX
    // rejections
    CHECK(!parse_u32("", &v));
    CHECK(!parse_u32("12x", &v));
    CHECK(!parse_u32("1.5", &v));
    CHECK(!parse_u32("+5", &v));  // leading sign
    CHECK(!parse_u32("-1", &v));
    CHECK(!parse_u32(" 5", &v));    // leading space
    CHECK(!parse_u32("0x10", &v));  // base-10 only
    CHECK(!parse_u32(NULL, &v));
}

static void test_settings_codec_roundtrip(void) {
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.baud = 9600;
    s.data_bits = 8;
    s.parity = 2;
    s.stop_bits = 1;
    strcpy(s.relay_name[0], "pump");
    strcpy(s.relay_name[2], "heater");

    uint8_t rec[SETTINGS_RECORD_LEN];
    CHECK(settings_codec_encode(rec, &s, 7) == SETTINGS_RECORD_LEN);

    settings_t out;
    memset(&out, 0, sizeof(out));
    uint32_t seq = 0;
    CHECK(settings_codec_decode(rec, &out, &seq));
    CHECK(seq == 7);
    CHECK(memcmp(&s, &out, sizeof(s)) == 0);
    CHECK(out.baud == 9600 && out.parity == 2);
    CHECK(strcmp(out.relay_name[0], "pump") == 0);
    CHECK(strcmp(out.relay_name[2], "heater") == 0);
}

static void test_settings_codec_rejects(void) {
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.baud = 115200;
    uint8_t rec[SETTINGS_RECORD_LEN];
    settings_codec_encode(rec, &s, 1);

    settings_t out;
    uint32_t seq;

    // tampered payload -> CRC fails
    rec[SC_OFF_PAYLOAD_V2] ^= 0xFF;
    CHECK(!settings_codec_decode(rec, &out, &seq));
    rec[SC_OFF_PAYLOAD_V2] ^= 0xFF;  // restore
    CHECK(settings_codec_decode(rec, &out, &seq));

    // wrong magic -> rejected
    rec[0] ^= 0xFF;
    CHECK(!settings_codec_decode(rec, &out, &seq));
    rec[0] ^= 0xFF;

    // a v2 record must not decode as v1
    CHECK(!settings_codec_decode_v1(rec, &out));
}

static void test_settings_codec_v1(void) {
    // Hand-build a legacy v1 record: magic@0, version=1@4, payload@8, crc after.
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.baud = 57600;
    s.data_bits = 7;
    s.parity = 1;
    s.stop_bits = 2;
    strcpy(s.relay_name[1], "alpha");

    uint8_t rec[SC_OFF_PAYLOAD_V1 + sizeof(settings_t) + 4];
    uint32_t magic = SETTINGS_MAGIC, ver = 1u;
    memcpy(rec + SC_OFF_MAGIC, &magic, sizeof(magic));
    memcpy(rec + SC_OFF_VERSION, &ver, sizeof(ver));
    memcpy(rec + SC_OFF_PAYLOAD_V1, &s, sizeof(s));
    size_t crc_off = SC_OFF_PAYLOAD_V1 + sizeof(s);
    uint32_t crc = dutler_crc32(rec, crc_off);
    memcpy(rec + crc_off, &crc, sizeof(crc));

    settings_t out;
    memset(&out, 0, sizeof(out));
    CHECK(settings_codec_decode_v1(rec, &out));
    CHECK(out.baud == 57600 && out.data_bits == 7 && out.parity == 1 && out.stop_bits == 2);
    CHECK(strcmp(out.relay_name[1], "alpha") == 0);

    // a v1 record must not decode as v2 (version mismatch)
    uint32_t seq;
    CHECK(!settings_codec_decode(rec, &out, &seq));
}

int main(void) {
    test_crc32();
    test_parse_u32();
    test_settings_codec_roundtrip();
    test_settings_codec_rejects();
    test_settings_codec_v1();

    printf("%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
