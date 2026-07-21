// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the power-loss-safe A/B settings store (settings.c) against a
// RAM-backed flash fake. Covers slot selection, CRC fallback, the v1->v2
// migration, and an interrupted ("power loss") save.

#include <string.h>

#include "config.h"
#include "core/settings.h"
#include "core/settings_codec.h"
#include "fakes.h"
#include "platform/flash_port.h"
#include "unity.h"
#include "util/crc32.h"

// Slot offsets derived from the (fake) flash size, exactly as settings.c does.
static uint32_t slot_a(void) { return flash_port_size() - 2u * FLASH_PORT_SECTOR_SIZE; }
static uint32_t slot_b(void) { return flash_port_size() - 1u * FLASH_PORT_SECTOR_SIZE; }
#define LEGACY (slot_b())  // a v1 record lived in the very last sector

void setUp(void) { flash_fake_reset(); }
void tearDown(void) {}

// Frozen size of the gen-1 payload (v1 and v2 records) — no device_name. The
// live settings_t is larger now, so records must be built with this size, and
// the first V1_PAYLOAD bytes of a settings_t are exactly that layout.
#define V1_PAYLOAD (4u + 1u + 1u + 1u + (OUT_COUNT * OUT_NAME_MAX) + 1u)

// Frozen size of the gen-2 payload (v3 records) — adds device_name, no shell.
#define V2_PAYLOAD (V1_PAYLOAD + DEVICE_NAME_MAX)

// Plant a valid legacy v1 record (magic, version=1, payload, crc) at `off`.
static void plant_v1(uint32_t off, const settings_t *s) {
    uint8_t rec[SC_OFF_PAYLOAD_V1 + V1_PAYLOAD + 4];
    uint32_t magic = SETTINGS_MAGIC, ver = 1u;
    memcpy(rec + SC_OFF_MAGIC, &magic, sizeof(magic));
    memcpy(rec + SC_OFF_VERSION, &ver, sizeof(ver));
    memcpy(rec + SC_OFF_PAYLOAD_V1, s, V1_PAYLOAD);
    size_t crc_off = SC_OFF_PAYLOAD_V1 + V1_PAYLOAD;
    uint32_t crc = dutler_crc32(rec, crc_off);
    memcpy(rec + crc_off, &crc, sizeof(crc));
    flash_fake_poke(off, rec, (uint32_t)sizeof(rec));
}

// Plant a valid older v2 record (magic, version=2, seq, gen-1 payload, crc).
static void plant_v2(uint32_t off, const settings_t *s, uint32_t seq) {
    uint8_t rec[SC_OFF_PAYLOAD_V2 + V1_PAYLOAD + 4];
    uint32_t magic = SETTINGS_MAGIC, ver = 2u;
    memcpy(rec + SC_OFF_MAGIC, &magic, sizeof(magic));
    memcpy(rec + SC_OFF_VERSION, &ver, sizeof(ver));
    memcpy(rec + SC_OFF_SEQ, &seq, sizeof(seq));
    memcpy(rec + SC_OFF_PAYLOAD_V2, s, V1_PAYLOAD);
    size_t crc_off = SC_OFF_PAYLOAD_V2 + V1_PAYLOAD;
    uint32_t crc = dutler_crc32(rec, crc_off);
    memcpy(rec + crc_off, &crc, sizeof(crc));
    flash_fake_poke(off, rec, (uint32_t)sizeof(rec));
}

// Plant a valid older v3 record (magic, version=3, seq, gen-2 payload, crc).
static void plant_v3(uint32_t off, const settings_t *s, uint32_t seq) {
    uint8_t rec[SC_OFF_PAYLOAD_V3 + V2_PAYLOAD + 4];
    uint32_t magic = SETTINGS_MAGIC, ver = 3u;
    memcpy(rec + SC_OFF_MAGIC, &magic, sizeof(magic));
    memcpy(rec + SC_OFF_VERSION, &ver, sizeof(ver));
    memcpy(rec + SC_OFF_SEQ, &seq, sizeof(seq));
    memcpy(rec + SC_OFF_PAYLOAD_V3, s, V2_PAYLOAD);
    size_t crc_off = SC_OFF_PAYLOAD_V3 + V2_PAYLOAD;
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

// The point of the runtime flash_port_size(): on a larger board (Pico 2, 4 MB)
// the A/B slots must move to the last two sectors of *that* flash, not stay at the
// 2 MB layout. Exercises the offset arithmetic end-to-end at 4 MB and guards
// against any regression to a hardcoded 2 MB offset.
static void test_slots_track_4mb_flash(void) {
    flash_fake_set_size(4u * 1024u * 1024u);

    // Offsets follow the new size, not the 2 MB default.
    TEST_ASSERT_EQUAL_UINT32(4u * 1024u * 1024u - 2u * FLASH_PORT_SECTOR_SIZE, slot_a());
    TEST_ASSERT_EQUAL_UINT32(4u * 1024u * 1024u - 1u * FLASH_PORT_SECTOR_SIZE, slot_b());

    settings_load();  // blank region -> defaults
    g_settings.baud = 460800;
    TEST_ASSERT_TRUE(settings_save());  // -> slot A, seq 1
    g_settings.baud = 921600;
    TEST_ASSERT_TRUE(settings_save());  // -> slot B, seq 2 (freshest)

    // The freshest record really landed in the 4 MB slot B.
    settings_t s;
    uint32_t seq;
    TEST_ASSERT_TRUE(settings_codec_decode(flash_port_read(slot_b()), &s, &seq));
    TEST_ASSERT_EQUAL_UINT32(2, seq);
    TEST_ASSERT_EQUAL_UINT32(921600, s.baud);

    // Nothing was written at the old 2 MB offset (which is mid-flash on a 4 MB
    // board) — that address must still read as erased. This is the assertion that
    // fails if the offsets ever regress to a hardcoded 2 MB.
    const uint8_t *stale = flash_port_read(2u * 1024u * 1024u - 1u * FLASH_PORT_SECTOR_SIZE);
    settings_t junk;
    uint32_t junk_seq;
    TEST_ASSERT_FALSE(settings_codec_decode(stale, &junk, &junk_seq));

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT32(921600, g_settings.baud);  // newest wins at 4 MB
}

// An older v2 record (A/B, no device_name/shell) is migrated with the appended
// fields cleared, and re-saved as a current record with the next sequence number.
static void test_v2_migration(void) {
    settings_t v2;
    memset(&v2, 0, sizeof(v2));
    v2.baud = 19200;
    v2.data_bits = 8;
    v2.stop_bits = 1;
    v2.echo = 1;
    strcpy(v2.out_name[2], "relay");
    plant_v2(slot_a(), &v2, 5);  // only the first V1_PAYLOAD bytes are stored

    settings_load();  // no v3/v4 record -> migrate the v2 record
    TEST_ASSERT_EQUAL_UINT32(19200, g_settings.baud);
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.echo);
    TEST_ASSERT_EQUAL_STRING("relay", g_settings.out_name[2]);
    TEST_ASSERT_EQUAL_STRING("", g_settings.device_name);  // absent in v2 -> cleared
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.shell);          // absent in v2 -> cleared

    // Re-saved as a current (v4) record in the other slot, seq bumped past 5.
    settings_t cur;
    uint32_t seq;
    TEST_ASSERT_TRUE(settings_codec_decode(flash_port_read(slot_b()), &cur, &seq));
    TEST_ASSERT_EQUAL_UINT32(19200, cur.baud);
    TEST_ASSERT_EQUAL_UINT32(6, seq);
}

// An older v3 record (A/B, has device_name, no shell) is migrated to v4 with the
// shell flag cleared, and re-saved as a current record with the next sequence.
static void test_v3_migration(void) {
    settings_t v3;
    memset(&v3, 0, sizeof(v3));
    v3.baud = 38400;
    v3.data_bits = 8;
    v3.stop_bits = 1;
    v3.echo = 1;
    strcpy(v3.out_name[0], "pump");
    strcpy(v3.device_name, "bench");
    plant_v3(slot_a(), &v3, 7);  // only the first V2_PAYLOAD bytes are stored

    settings_load();  // no v4 record -> migrate the v3 record
    TEST_ASSERT_EQUAL_UINT32(38400, g_settings.baud);
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.echo);
    TEST_ASSERT_EQUAL_STRING("pump", g_settings.out_name[0]);
    TEST_ASSERT_EQUAL_STRING("bench", g_settings.device_name);
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.shell);  // absent in v3 -> cleared

    // Re-saved as a current (v4) record in the other slot, seq bumped past 7.
    settings_t cur;
    uint32_t seq;
    TEST_ASSERT_TRUE(settings_codec_decode(flash_port_read(slot_b()), &cur, &seq));
    TEST_ASSERT_EQUAL_STRING("bench", cur.device_name);
    TEST_ASSERT_EQUAL_UINT32(8, seq);
}

// A device name persists through a v4 save/load round-trip.
static void test_device_name_roundtrip(void) {
    settings_load();
    strcpy(g_settings.device_name, "arrakeen-rpi5");
    TEST_ASSERT_TRUE(settings_save());

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_STRING("arrakeen-rpi5", g_settings.device_name);
}

// The shell flag persists through a v4 save/load round-trip.
static void test_shell_roundtrip(void) {
    settings_load();
    g_settings.shell = 1;
    TEST_ASSERT_TRUE(settings_save());

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.shell);
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
    RUN_TEST(test_v2_migration);
    RUN_TEST(test_v3_migration);
    RUN_TEST(test_device_name_roundtrip);
    RUN_TEST(test_shell_roundtrip);
    RUN_TEST(test_slots_track_4mb_flash);
    RUN_TEST(test_factory_reset);
    return UNITY_END();
}
