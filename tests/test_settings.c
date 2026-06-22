// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the power-loss-safe A/B settings store (settings.c) against a
// RAM-backed flash fake. Covers slot selection, CRC fallback, the v1->v2
// migration, and an interrupted ("power loss") save.

#include <string.h>

#include "config.h"
#include "fakes.h"
#include "flash_port.h"
#include "settings.h"
#include "settings_codec.h"
#include "unity.h"
#include "util/crc32.h"

// Slot offsets derived from the (fake) flash size, exactly as settings.c does.
static uint32_t slot_a(void) { return flash_port_size() - 2u * FLASH_PORT_SECTOR_SIZE; }
static uint32_t slot_b(void) { return flash_port_size() - 1u * FLASH_PORT_SECTOR_SIZE; }
#define LEGACY (slot_b())  // a v1 record lived in the very last sector

void setUp(void) { flash_fake_reset(); }
void tearDown(void) {}

// Plant a valid legacy v1 record (magic, version=1, payload, crc) at `off`.
static void plant_v1(uint32_t off, const settings_t *s) {
    uint8_t rec[SC_OFF_PAYLOAD_V1 + sizeof(settings_t) + 4];
    uint32_t magic = SETTINGS_MAGIC, ver = 1u;
    memcpy(rec + SC_OFF_MAGIC, &magic, sizeof(magic));
    memcpy(rec + SC_OFF_VERSION, &ver, sizeof(ver));
    memcpy(rec + SC_OFF_PAYLOAD_V1, s, sizeof(*s));
    size_t crc_off = SC_OFF_PAYLOAD_V1 + sizeof(*s);
    uint32_t crc = dutler_crc32(rec, crc_off);
    memcpy(rec + crc_off, &crc, sizeof(crc));
    flash_fake_poke(off, rec, (uint32_t)sizeof(rec));
}

static void test_defaults_when_blank(void) {
    settings_load();
    TEST_ASSERT_EQUAL_UINT32(BRIDGE_INIT_BAUD, g_settings.baud);
    TEST_ASSERT_EQUAL_UINT8(8, g_settings.data_bits);
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.parity);
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.stop_bits);
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);
}

static void test_save_load_roundtrip(void) {
    settings_load();
    g_settings.baud = 230400;
    g_settings.parity = 2;
    strcpy(g_settings.out_name[0], "pump");
    TEST_ASSERT_TRUE(settings_save());

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT32(230400, g_settings.baud);
    TEST_ASSERT_EQUAL_UINT8(2, g_settings.parity);
    TEST_ASSERT_EQUAL_STRING("pump", g_settings.out_name[0]);
}

// Two consecutive saves land in different slots, each with its own sequence
// number; the newest valid slot wins on load.
static void test_ab_ping_pong(void) {
    settings_load();
    g_settings.baud = 1000000;
    TEST_ASSERT_TRUE(settings_save());  // seq 1
    g_settings.baud = 2000000;
    TEST_ASSERT_TRUE(settings_save());  // seq 2

    settings_t a, b;
    uint32_t seq_a, seq_b;
    TEST_ASSERT_TRUE(settings_codec_decode(flash_port_read(slot_a()), &a, &seq_a));
    TEST_ASSERT_TRUE(settings_codec_decode(flash_port_read(slot_b()), &b, &seq_b));
    // Distinct slots, distinct sequence numbers (1 and 2 in some order).
    TEST_ASSERT_TRUE(seq_a != seq_b);
    TEST_ASSERT_EQUAL_UINT32(3, seq_a + seq_b);
    // The slot with the higher seq holds the newest baud.
    TEST_ASSERT_EQUAL_UINT32(2000000, (seq_a > seq_b) ? a.baud : b.baud);

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT32(2000000, g_settings.baud);  // newest wins
}

// Corrupting the freshest slot makes load fall back to the older good slot.
static void test_crc_fallback(void) {
    settings_load();
    g_settings.baud = 1000000;
    TEST_ASSERT_TRUE(settings_save());  // -> slot A, seq 1
    g_settings.baud = 2000000;
    TEST_ASSERT_TRUE(settings_save());  // -> slot B, seq 2 (freshest)

    uint8_t junk = 0x00;  // clobber a payload byte in the freshest slot (B)
    flash_fake_poke(slot_b() + SC_OFF_PAYLOAD_V2, &junk, 1);

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT32(1000000, g_settings.baud);  // fell back to slot A
}

// A save interrupted between erase and program loses nothing: the previous good
// slot still loads.
static void test_power_loss_safe(void) {
    settings_load();
    g_settings.baud = 1000000;
    TEST_ASSERT_TRUE(settings_save());  // good config persisted

    g_settings.baud = 2000000;
    flash_fake_fail_next_program();
    TEST_ASSERT_FALSE(settings_save());  // verify read-back fails -> reports failure

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT32(1000000, g_settings.baud);  // last good config intact
}

static void test_v1_migration(void) {
    settings_t v1;
    memset(&v1, 0, sizeof(v1));
    v1.baud = 57600;
    v1.data_bits = 7;
    v1.parity = 1;
    v1.stop_bits = 2;
    strcpy(v1.out_name[1], "alpha");
    plant_v1(LEGACY, &v1);

    settings_load();  // no v2 record -> upgrade the v1 record in place
    TEST_ASSERT_EQUAL_UINT32(57600, g_settings.baud);
    TEST_ASSERT_EQUAL_UINT8(7, g_settings.data_bits);
    TEST_ASSERT_EQUAL_STRING("alpha", g_settings.out_name[1]);

    // It was re-saved as a valid v2 record (in slot A).
    settings_t v2;
    uint32_t seq;
    TEST_ASSERT_TRUE(settings_codec_decode(flash_port_read(slot_a()), &v2, &seq));
    TEST_ASSERT_EQUAL_UINT32(57600, v2.baud);
    TEST_ASSERT_EQUAL_STRING("alpha", v2.out_name[1]);
}

static void test_factory_reset(void) {
    settings_load();
    g_settings.baud = 9600;
    strcpy(g_settings.out_name[0], "pump");
    TEST_ASSERT_TRUE(settings_save());

    settings_reset();
    TEST_ASSERT_EQUAL_UINT32(BRIDGE_INIT_BAUD, g_settings.baud);
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();  // both slots erased -> defaults
    TEST_ASSERT_EQUAL_UINT32(BRIDGE_INIT_BAUD, g_settings.baud);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_when_blank);
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_ab_ping_pong);
    RUN_TEST(test_crc_fallback);
    RUN_TEST(test_power_loss_safe);
    RUN_TEST(test_v1_migration);
    RUN_TEST(test_factory_reset);
    return UNITY_END();
}
