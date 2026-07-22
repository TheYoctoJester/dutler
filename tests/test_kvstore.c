// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the user key/value store (kvstore.c) against the RAM flash fake:
// the RAM working copy (set/get/clear/replace), listing, length/capacity limits,
// and the A/B flash persistence (round-trip, CRC fallback, interrupted save).

#include <string.h>

#include "config.h"
#include "core/kvstore.h"
#include "fakes.h"
#include "platform/flash_port.h"
#include "unity.h"

// KV slot B sits just below the settings slots (last two sectors); slot A is one
// sector below that. The crc-fallback test corrupts the freshest slot (B).
static uint32_t kv_b(void) { return flash_port_size() - 3u * FLASH_PORT_SECTOR_SIZE; }

void setUp(void) {
    flash_fake_reset();
    kv_load();  // blank region -> empty store
}
void tearDown(void) {}

static int g_count;
static void count_cb(const char *k, const char *v) {
    (void)k;
    (void)v;
    g_count++;
}
static int kv_count(void) {
    g_count = 0;
    kv_foreach(count_cb);
    return g_count;
}

static void test_empty_when_blank(void) {
    TEST_ASSERT_NULL(kv_get("x"));
    TEST_ASSERT_EQUAL_INT(0, kv_count());
    TEST_ASSERT_FALSE(kv_dirty());
}

static void test_set_get_dirty(void) {
    TEST_ASSERT_TRUE(kv_set("a", "1"));
    TEST_ASSERT_EQUAL_STRING("1", kv_get("a"));
    TEST_ASSERT_TRUE(kv_dirty());
    TEST_ASSERT_EQUAL_INT(1, kv_count());
}

static void test_replace(void) {
    kv_set("a", "1");
    kv_set("a", "22");
    TEST_ASSERT_EQUAL_STRING("22", kv_get("a"));
    TEST_ASSERT_EQUAL_INT(1, kv_count());  // replaced, not duplicated
}

static void test_clear(void) {
    kv_set("a", "1");
    kv_set("b", "2");
    TEST_ASSERT_TRUE(kv_clear("a"));
    TEST_ASSERT_NULL(kv_get("a"));
    TEST_ASSERT_EQUAL_STRING("2", kv_get("b"));  // neighbour intact
    TEST_ASSERT_FALSE(kv_clear("a"));            // already gone
    TEST_ASSERT_FALSE(kv_clear("nope"));
}

static void test_value_with_spaces(void) {
    TEST_ASSERT_TRUE(kv_set("note", "hello world  x"));
    TEST_ASSERT_EQUAL_STRING("hello world  x", kv_get("note"));
}

static void test_length_limits(void) {
    char key[KV_KEY_MAX + 8];
    memset(key, 'k', sizeof(key));
    key[KV_KEY_MAX - 1] = '\0';  // KV_KEY_MAX-1 chars -> fits (incl NUL == KV_KEY_MAX)
    TEST_ASSERT_TRUE(kv_set(key, "v"));
    key[KV_KEY_MAX - 1] = 'k';  // restore, so the terminator moves out one
    key[KV_KEY_MAX] = '\0';     // KV_KEY_MAX chars -> too long
    TEST_ASSERT_FALSE(kv_set(key, "v"));

    char val[KV_VALUE_MAX + 8];
    memset(val, 'x', sizeof(val));
    val[KV_VALUE_MAX] = '\0';  // KV_VALUE_MAX chars -> too long
    TEST_ASSERT_FALSE(kv_set("k", val));
}

static void test_store_full(void) {
    char val[100];
    memset(val, 'x', sizeof(val));
    val[99] = '\0';
    int set_ok = 0;
    for (int i = 0; i < 100; i++) {
        char key[8];
        snprintf(key, sizeof(key), "k%d", i);
        if (!kv_set(key, val)) break;
        set_ok++;
    }
    TEST_ASSERT_TRUE(set_ok > 0);        // some fit
    TEST_ASSERT_TRUE(set_ok < 100);      // and it eventually filled up
    TEST_ASSERT_NOT_NULL(kv_get("k0"));  // earlier entries survive a full store
}

static void test_persist_roundtrip(void) {
    kv_set("a", "1");
    kv_set("b", "two words");
    TEST_ASSERT_TRUE(kv_save());
    TEST_ASSERT_FALSE(kv_dirty());

    kv_load();  // reload from flash
    TEST_ASSERT_EQUAL_STRING("1", kv_get("a"));
    TEST_ASSERT_EQUAL_STRING("two words", kv_get("b"));
    TEST_ASSERT_EQUAL_INT(2, kv_count());
}

// Corrupting the freshest slot makes load fall back to the older good one.
static void test_crc_fallback(void) {
    kv_set("a", "1");
    TEST_ASSERT_TRUE(kv_save());  // -> slot A (seq 1)
    kv_set("a", "2");
    TEST_ASSERT_TRUE(kv_save());  // -> slot B (seq 2, freshest)

    uint8_t junk = 0x00;
    flash_fake_poke(kv_b(), &junk, 1);  // clobber slot B's magic

    kv_load();
    TEST_ASSERT_EQUAL_STRING("1", kv_get("a"));  // fell back to slot A
}

// A save interrupted between erase and program loses nothing.
static void test_power_loss_safe(void) {
    kv_set("a", "1");
    TEST_ASSERT_TRUE(kv_save());

    kv_set("a", "2");
    flash_fake_fail_next_program();
    TEST_ASSERT_FALSE(kv_save());  // verify read-back fails -> reports failure

    kv_load();
    TEST_ASSERT_EQUAL_STRING("1", kv_get("a"));  // last good store intact
}

static void test_reset(void) {
    kv_set("a", "1");
    TEST_ASSERT_TRUE(kv_save());
    kv_reset();
    TEST_ASSERT_NULL(kv_get("a"));
    TEST_ASSERT_EQUAL_INT(0, kv_count());
    kv_load();  // both slots erased -> empty
    TEST_ASSERT_EQUAL_INT(0, kv_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_when_blank);
    RUN_TEST(test_set_get_dirty);
    RUN_TEST(test_replace);
    RUN_TEST(test_clear);
    RUN_TEST(test_value_with_spaces);
    RUN_TEST(test_length_limits);
    RUN_TEST(test_store_full);
    RUN_TEST(test_persist_roundtrip);
    RUN_TEST(test_crc_fallback);
    RUN_TEST(test_power_loss_safe);
    RUN_TEST(test_reset);
    return UNITY_END();
}
