// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Pure-logic unit tests (no SDK): CRC-32, integer parsing, settings record codec.

#include <string.h>

#include "core/settings_codec.h"
#include "unity.h"
#include "util/crc32.h"
#include "util/numparse.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc32(void) {
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, dutler_crc32("123456789", 9));  // standard check value
    TEST_ASSERT_EQUAL_HEX32(0u, dutler_crc32("", 0));                    // init ^ xorout
    TEST_ASSERT_TRUE(dutler_crc32("hello", 5) != dutler_crc32("hellp", 5));
}

static void test_parse_u32(void) {
    uint32_t v = 123;
    TEST_ASSERT_TRUE(parse_u32("0", &v) && v == 0);
    TEST_ASSERT_TRUE(parse_u32("115200", &v) && v == 115200u);
    TEST_ASSERT_TRUE(parse_u32("4294967295", &v) && v == 4294967295u);  // UINT32_MAX
    TEST_ASSERT_FALSE(parse_u32("", &v));
    TEST_ASSERT_FALSE(parse_u32("12x", &v));
    TEST_ASSERT_FALSE(parse_u32("1.5", &v));
    TEST_ASSERT_FALSE(parse_u32("+5", &v));  // leading sign
    TEST_ASSERT_FALSE(parse_u32("-1", &v));
    TEST_ASSERT_FALSE(parse_u32(" 5", &v));    // leading space
    TEST_ASSERT_FALSE(parse_u32("0x10", &v));  // base-10 only
    TEST_ASSERT_FALSE(parse_u32(NULL, &v));
}

static void test_codec_roundtrip(void) {
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.baud = 9600;
    s.data_bits = 8;
    s.parity = 2;
    s.stop_bits = 1;
    strcpy(s.out_name[0], "pump");
    strcpy(s.out_name[2], "heater");

    uint8_t rec[SETTINGS_RECORD_LEN];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SETTINGS_RECORD_LEN,
                             (uint32_t)settings_codec_encode(rec, &s, 7));

    settings_t out;
    memset(&out, 0, sizeof(out));
    uint32_t seq = 0;
    TEST_ASSERT_TRUE(settings_codec_decode(rec, &out, &seq));
    TEST_ASSERT_EQUAL_UINT32(7, seq);
    TEST_ASSERT_EQUAL_MEMORY(&s, &out, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("pump", out.out_name[0]);
    TEST_ASSERT_EQUAL_STRING("heater", out.out_name[2]);
}

static void test_codec_rejects(void) {
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.baud = 115200;
    uint8_t rec[SETTINGS_RECORD_LEN];
    settings_codec_encode(rec, &s, 1);

    settings_t out;
    uint32_t seq;

    rec[SC_OFF_PAYLOAD_V2] ^= 0xFF;  // tampered payload -> CRC fails
    TEST_ASSERT_FALSE(settings_codec_decode(rec, &out, &seq));
    rec[SC_OFF_PAYLOAD_V2] ^= 0xFF;  // restore
    TEST_ASSERT_TRUE(settings_codec_decode(rec, &out, &seq));

    rec[0] ^= 0xFF;  // wrong magic -> rejected
    TEST_ASSERT_FALSE(settings_codec_decode(rec, &out, &seq));
    rec[0] ^= 0xFF;

    TEST_ASSERT_FALSE(settings_codec_decode_v1(rec, &out));  // a v2 record is not v1
}

static void test_codec_v1(void) {
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.baud = 57600;
    s.data_bits = 7;
    s.parity = 1;
    s.stop_bits = 2;
    strcpy(s.out_name[1], "alpha");

    // A real v1 record carries the frozen gen-1 payload (no device_name); the
    // first V1_PAYLOAD bytes of a settings_t are exactly that layout.
    const size_t v1_payload = 4u + 1u + 1u + 1u + (OUT_COUNT * OUT_NAME_MAX) + 1u;
    uint8_t rec[SC_OFF_PAYLOAD_V1 + sizeof(settings_t) + 4];
    uint32_t magic = SETTINGS_MAGIC, ver = 1u;
    memcpy(rec + SC_OFF_MAGIC, &magic, sizeof(magic));
    memcpy(rec + SC_OFF_VERSION, &ver, sizeof(ver));
    memcpy(rec + SC_OFF_PAYLOAD_V1, &s, v1_payload);
    size_t crc_off = SC_OFF_PAYLOAD_V1 + v1_payload;
    uint32_t crc = dutler_crc32(rec, crc_off);
    memcpy(rec + crc_off, &crc, sizeof(crc));

    settings_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(settings_codec_decode_v1(rec, &out));
    TEST_ASSERT_EQUAL_UINT32(57600, out.baud);
    TEST_ASSERT_EQUAL_STRING("alpha", out.out_name[1]);
    TEST_ASSERT_EQUAL_STRING("", out.device_name);  // absent in v1 -> cleared

    uint32_t seq;
    TEST_ASSERT_FALSE(settings_codec_decode(rec, &out, &seq));  // v1 is not the current version
}

static void test_codec_v3(void) {
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.baud = 38400;
    s.echo = 1;
    strcpy(s.device_name, "bench");
    s.shell = 1;  // set in the source struct, but a v3 record cannot carry it

    // A real v3 record carries the frozen gen-2 payload (echo + device_name, no
    // shell); the first V2_PAYLOAD bytes of a settings_t are exactly that layout.
    const size_t v2_payload = 4u + 1u + 1u + 1u + (OUT_COUNT * OUT_NAME_MAX) + 1u + DEVICE_NAME_MAX;
    uint8_t rec[SC_OFF_PAYLOAD_V3 + sizeof(settings_t) + 4];
    uint32_t magic = SETTINGS_MAGIC, ver = 3u, seq_in = 9u;
    memcpy(rec + SC_OFF_MAGIC, &magic, sizeof(magic));
    memcpy(rec + SC_OFF_VERSION, &ver, sizeof(ver));
    memcpy(rec + SC_OFF_SEQ, &seq_in, sizeof(seq_in));
    memcpy(rec + SC_OFF_PAYLOAD_V3, &s, v2_payload);
    size_t crc_off = SC_OFF_PAYLOAD_V3 + v2_payload;
    uint32_t crc = dutler_crc32(rec, crc_off);
    memcpy(rec + crc_off, &crc, sizeof(crc));

    settings_t out;
    uint32_t seq = 0;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(settings_codec_decode_v3(rec, &out, &seq));
    TEST_ASSERT_EQUAL_UINT32(9, seq);
    TEST_ASSERT_EQUAL_UINT32(38400, out.baud);
    TEST_ASSERT_EQUAL_STRING("bench", out.device_name);
    TEST_ASSERT_EQUAL_UINT8(0, out.shell);  // absent in v3 -> cleared

    settings_t o2;
    uint32_t s2;
    TEST_ASSERT_FALSE(settings_codec_decode(rec, &o2, &s2));  // v3 is not current
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32);
    RUN_TEST(test_parse_u32);
    RUN_TEST(test_codec_roundtrip);
    RUN_TEST(test_codec_rejects);
    RUN_TEST(test_codec_v1);
    RUN_TEST(test_codec_v3);
    return UNITY_END();
}
