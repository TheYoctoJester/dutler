// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the control-port command interpreter (command.c). Drives
// command_dispatch() and asserts on the captured console output and the real
// output/settings state, with the hardware seams faked.

#include <string.h>

#include "config.h"
#include "core/command.h"
#include "core/outputs.h"
#include "core/settings.h"
#include "fakes.h"
#include "unity.h"

void setUp(void) {
    flash_fake_reset();
    settings_load();  // defaults
    outputs_init();   // all outputs OFF
    fake_console_clear();
    fake_bootsel_clear();
    fake_bridge_set_selftest(true);
}
void tearDown(void) {}

// command_dispatch() tokenizes in place, so feed it a writable copy.
static void run(const char *line) {
    char buf[128];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    command_dispatch(buf);
}

#define ASSERT_SAID(needle) \
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(fake_console_text(), needle), needle)
#define ASSERT_NOT_SAID(needle) \
    TEST_ASSERT_NULL_MESSAGE(strstr(fake_console_text(), needle), needle)

static void test_status_and_help_and_version(void) {
    run("status");
    ASSERT_SAID("out 1 off");
    ASSERT_SAID("out 4 off");
    ASSERT_SAID("bridge default 115200");
    ASSERT_SAID("firmware DUTler");

    fake_console_clear();
    run("help");
    ASSERT_SAID("commands");

    fake_console_clear();
    run("version");
    ASSERT_SAID("DUTler");
}

static void test_out_on_off_toggle(void) {
    run("out 1 on");
    TEST_ASSERT_TRUE(outputs_get(0));
    ASSERT_SAID("ok");

    run("out 1 off");
    TEST_ASSERT_FALSE(outputs_get(0));

    run("out 1 toggle");
    TEST_ASSERT_TRUE(outputs_get(0));
    run("out 1 toggle");
    TEST_ASSERT_FALSE(outputs_get(0));
}

static void test_number_shorthand(void) {
    run("2 on");
    TEST_ASSERT_TRUE(outputs_get(1));
    ASSERT_SAID("ok");
}

static void test_name_and_name_shorthand(void) {
    run("name 1 pump");
    ASSERT_SAID("ok");
    TEST_ASSERT_EQUAL_STRING("pump", g_settings.out_name[0]);

    fake_console_clear();
    run("pump on");  // shorthand by name
    TEST_ASSERT_TRUE(outputs_get(0));
    ASSERT_SAID("ok");

    run("name 1 clear");
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);
}

static void test_name_validation(void) {
    run("name 1 123");
    ASSERT_SAID("all digits");
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);

    fake_console_clear();
    run("name 1 out");  // reserved command word
    ASSERT_SAID("command word");

    fake_console_clear();
    run("name 1 selftest");  // every dispatch verb must be reserved, incl. selftest
    ASSERT_SAID("command word");
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);

    fake_console_clear();
    run("name 1 abcdefghijklmnopqrst");  // 20 chars, >= OUT_NAME_MAX
    ASSERT_SAID("too long");

    fake_console_clear();
    run("name 9 x");  // out of range
    ASSERT_SAID("out of range");
}

static void test_set_baud_format_and_dirty(void) {
    run("set baud 9600");
    TEST_ASSERT_EQUAL_UINT32(9600, g_settings.baud);

    fake_console_clear();
    run("status");
    ASSERT_SAID("unsaved changes");

    fake_console_clear();
    run("save");
    ASSERT_SAID("saved");

    fake_console_clear();
    run("status");
    ASSERT_NOT_SAID("unsaved changes");

    // persisted across a reload
    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT32(9600, g_settings.baud);

    // validation
    fake_console_clear();
    run("set baud 10");
    ASSERT_SAID("out of range");
    fake_console_clear();
    run("set format 9N1");
    ASSERT_SAID("data bits");
}

static void test_factory_reset(void) {
    run("set baud 9600");
    run("name 1 pump");
    run("save");
    fake_console_clear();

    run("factory-reset");  // bare: warns, does not reset
    ASSERT_SAID("erases all saved settings");
    TEST_ASSERT_EQUAL_UINT32(9600, g_settings.baud);

    fake_console_clear();
    run("factory-reset confirm");
    ASSERT_SAID("factory reset done");
    TEST_ASSERT_EQUAL_UINT32(BRIDGE_INIT_BAUD, g_settings.baud);
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);
}

static void test_selftest_reflects_bridge(void) {
    fake_bridge_set_selftest(true);
    run("selftest");
    ASSERT_SAID("continuity OK");

    fake_console_clear();
    fake_bridge_set_selftest(false);
    run("selftest");
    ASSERT_SAID("OPEN");
}

static void test_bootsel_requests_reboot(void) {
    run("bootsel");
    TEST_ASSERT_TRUE(fake_bootsel_requested());
    ASSERT_SAID("rebooting");

    fake_bootsel_clear();
    run("reset");  // alias
    TEST_ASSERT_TRUE(fake_bootsel_requested());
}

static void test_unknown_and_errors(void) {
    run("bogus");
    ASSERT_SAID("unknown command");

    fake_console_clear();
    run("out 9 on");  // out of range id
    ASSERT_SAID("unknown output");

    fake_console_clear();
    run("out 1 sideways");  // bad action
    ASSERT_SAID("unknown output command");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_status_and_help_and_version);
    RUN_TEST(test_out_on_off_toggle);
    RUN_TEST(test_number_shorthand);
    RUN_TEST(test_name_and_name_shorthand);
    RUN_TEST(test_name_validation);
    RUN_TEST(test_set_baud_format_and_dirty);
    RUN_TEST(test_factory_reset);
    RUN_TEST(test_selftest_reflects_bridge);
    RUN_TEST(test_bootsel_requests_reboot);
    RUN_TEST(test_unknown_and_errors);
    return UNITY_END();
}
