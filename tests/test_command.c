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

static void test_status_and_help(void) {
    run("status");
    ASSERT_SAID("out 1 off");
    ASSERT_SAID("out 4 off");
    ASSERT_SAID("bridge default 115200");
    ASSERT_SAID("firmware DUTler");

    fake_console_clear();
    run("help");
    ASSERT_SAID("commands");
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

static void test_outname_and_shorthand(void) {
    run("set outname 1 pump");
    ASSERT_SAID("ok");
    TEST_ASSERT_EQUAL_STRING("pump", g_settings.out_name[0]);

    fake_console_clear();
    run("pump on");  // shorthand by name
    TEST_ASSERT_TRUE(outputs_get(0));
    ASSERT_SAID("ok");

    run("set outname 1 clear");
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);
}

static void test_outname_validation(void) {
    run("set outname 1 123");
    ASSERT_SAID("all digits");
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);

    fake_console_clear();
    run("set outname 1 out");  // reserved command word
    ASSERT_SAID("command word");

    fake_console_clear();
    run("set outname 1 selftest");  // every dispatch verb must be reserved, incl. selftest
    ASSERT_SAID("command word");
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);

    fake_console_clear();
    run("set outname 1 abcdefghijklmnopqrst");  // 20 chars, >= OUT_NAME_MAX
    ASSERT_SAID("too long");

    fake_console_clear();
    run("set outname 9 x");  // out of range
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

static void test_set_echo(void) {
    // Off by default, and reported by status.
    run("status");
    ASSERT_SAID("echo off");

    fake_console_clear();
    run("set echo on");
    ASSERT_SAID("ok");
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.echo);

    fake_console_clear();
    run("status");
    ASSERT_SAID("echo on");

    // Persisted across save + reload (rides in the old reserved byte).
    run("save");
    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.echo);

    // Validation + turn-off.
    fake_console_clear();
    run("set echo maybe");
    ASSERT_SAID("must be 'on' or 'off'");
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.echo);  // unchanged on bad value

    fake_console_clear();
    run("set echo off");
    ASSERT_SAID("ok");
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.echo);
}

static void test_set_shell(void) {
    run("status");
    fake_console_clear();
    run("get shell");
    ASSERT_SAID("shell off");  // off by default

    fake_console_clear();
    run("set shell on");
    ASSERT_SAID("ok");
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.shell);

    fake_console_clear();
    run("get shell");
    ASSERT_SAID("shell on");

    // Persisted across save + reload.
    run("save");
    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.shell);

    // Validation + turn-off.
    fake_console_clear();
    run("set shell maybe");
    ASSERT_SAID("must be 'on' or 'off'");
    TEST_ASSERT_EQUAL_UINT8(1, g_settings.shell);  // unchanged on bad value

    fake_console_clear();
    run("set shell off");
    ASSERT_SAID("ok");
    TEST_ASSERT_EQUAL_UINT8(0, g_settings.shell);
}

static void test_factory_reset(void) {
    run("set baud 9600");
    run("set outname 1 pump");
    run("set dutname bench");  // part of the live USB identity
    run("save");
    fake_console_clear();

    run("factory-reset");  // bare: warns, does not reset
    ASSERT_SAID("erases all saved settings");
    TEST_ASSERT_EQUAL_UINT32(9600, g_settings.baud);

    fake_console_clear();
    fake_reenumerate_clear();
    run("factory-reset confirm");
    ASSERT_SAID("factory reset done");
    TEST_ASSERT_EQUAL_UINT32(BRIDGE_INIT_BAUD, g_settings.baud);
    TEST_ASSERT_EQUAL_STRING("", g_settings.out_name[0]);
    TEST_ASSERT_EQUAL_STRING("", g_settings.device_name);
    TEST_ASSERT_EQUAL_INT(1, fake_reenumerate_count());  // by-id identity refreshed live
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

static void test_bootsel_and_reset(void) {
    run("bootsel");
    TEST_ASSERT_TRUE(fake_bootsel_requested());
    ASSERT_SAID("BOOTSEL");

    // 'reset' is a warm reboot into the application, NOT a bootsel.
    fake_console_clear();
    fake_bootsel_clear();
    fake_reboot_clear();
    run("reset");
    ASSERT_SAID("rebooting");
    TEST_ASSERT_FALSE(fake_bootsel_requested());
    TEST_ASSERT_EQUAL_INT(1, fake_reboot_count());
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

static void test_readonly_props(void) {
    run("get serial");
    ASSERT_SAID("serial TESTSERIAL000001");

    fake_console_clear();
    run("get version");
    ASSERT_SAID("version");

    // serial and version are read-only device properties; 'set' is rejected.
    fake_console_clear();
    run("set serial abc");
    ASSERT_SAID("read-only");

    fake_console_clear();
    run("set version 9");
    ASSERT_SAID("read-only");
}

static void test_set_dutname(void) {
    fake_reenumerate_clear();
    run("set dutname arrakeen-rpi5");
    ASSERT_SAID("ok");
    TEST_ASSERT_EQUAL_STRING("arrakeen-rpi5", g_settings.device_name);
    TEST_ASSERT_EQUAL_INT(1, fake_reenumerate_count());  // applied live via re-enumeration

    fake_console_clear();
    run("get dutname");
    ASSERT_SAID("dutname arrakeen-rpi5");

    // Illegal charset: rejected, name unchanged, no re-enumeration.
    fake_console_clear();
    fake_reenumerate_clear();
    run("set dutname a/b");
    ASSERT_SAID("error");
    TEST_ASSERT_EQUAL_STRING("arrakeen-rpi5", g_settings.device_name);
    TEST_ASSERT_EQUAL_INT(0, fake_reenumerate_count());

    // Too long (>= DEVICE_NAME_MAX): rejected.
    fake_console_clear();
    run("set dutname aaaaaaaaaaaaaaaaaaaaaaaaaaaa");  // 28 chars
    ASSERT_SAID("error");
    TEST_ASSERT_EQUAL_STRING("arrakeen-rpi5", g_settings.device_name);

    // Clear.
    run("set dutname clear");
    TEST_ASSERT_EQUAL_STRING("", g_settings.device_name);
}

static void test_get_all(void) {
    run("set outname 2 relay");
    fake_console_clear();
    run("get");  // no key: dump the whole store
    ASSERT_SAID("baud 115200");
    ASSERT_SAID("format 8N1");
    ASSERT_SAID("echo off");
    ASSERT_SAID("shell off");
    ASSERT_SAID("dutname");
    ASSERT_SAID("outname 1");
    ASSERT_SAID("outname 2 relay");
    ASSERT_SAID("outname 4");
    ASSERT_SAID("serial TESTSERIAL000001");
    ASSERT_SAID("version");
}

static bool cand_has(const char **c, size_t n, const char *s) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(c[i], s) == 0) return true;
    return false;
}

static void test_command_complete(void) {
    const char *c[24];
    size_t n;

    // Token 0: command verbs by prefix. "se" -> set, selftest.
    n = command_complete("se", 2, c, 24);
    TEST_ASSERT_TRUE(cand_has(c, n, "set"));
    TEST_ASSERT_TRUE(cand_has(c, n, "selftest"));
    TEST_ASSERT_FALSE(cand_has(c, n, "get"));

    // set <key>: settable keys (no read-only serial/version).
    n = command_complete("set ", 4, c, 24);
    TEST_ASSERT_TRUE(cand_has(c, n, "baud"));
    TEST_ASSERT_TRUE(cand_has(c, n, "shell"));
    TEST_ASSERT_TRUE(cand_has(c, n, "outname"));
    TEST_ASSERT_FALSE(cand_has(c, n, "serial"));

    // get <key>: includes the read-only properties.
    n = command_complete("get ", 4, c, 24);
    TEST_ASSERT_TRUE(cand_has(c, n, "serial"));
    TEST_ASSERT_TRUE(cand_has(c, n, "version"));

    // Enum values for on/off keys.
    n = command_complete("set echo ", 9, c, 24);
    TEST_ASSERT_TRUE(cand_has(c, n, "on"));
    TEST_ASSERT_TRUE(cand_has(c, n, "off"));
    n = command_complete("set shell ", 10, c, 24);
    TEST_ASSERT_TRUE(cand_has(c, n, "on"));

    // 'clear' after dutname / outname <n>.
    n = command_complete("set dutname ", 12, c, 24);
    TEST_ASSERT_TRUE(cand_has(c, n, "clear"));
    n = command_complete("set outname 1 ", 14, c, 24);
    TEST_ASSERT_TRUE(cand_has(c, n, "clear"));

    // Output names: as an 'out' target, as a shorthand verb, and among token-0.
    strcpy(g_settings.out_name[0], "pump");
    n = command_complete("out ", 4, c, 24);
    TEST_ASSERT_TRUE(cand_has(c, n, "pump"));
    n = command_complete("pump ", 5, c, 24);  // shorthand -> actions
    TEST_ASSERT_TRUE(cand_has(c, n, "on"));
    TEST_ASSERT_TRUE(cand_has(c, n, "toggle"));
    n = command_complete("pu", 2, c, 24);  // token 0 offers the output name
    TEST_ASSERT_TRUE(cand_has(c, n, "pump"));
}

static void test_get_single_and_unknown(void) {
    run("get baud");
    ASSERT_SAID("baud 115200");

    fake_console_clear();
    run("get outname 2");
    ASSERT_SAID("outname 2");

    fake_console_clear();
    run("get bogus");
    ASSERT_SAID("unknown key");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_status_and_help);
    RUN_TEST(test_readonly_props);
    RUN_TEST(test_set_dutname);
    RUN_TEST(test_get_all);
    RUN_TEST(test_get_single_and_unknown);
    RUN_TEST(test_command_complete);
    RUN_TEST(test_out_on_off_toggle);
    RUN_TEST(test_number_shorthand);
    RUN_TEST(test_outname_and_shorthand);
    RUN_TEST(test_outname_validation);
    RUN_TEST(test_set_baud_format_and_dirty);
    RUN_TEST(test_set_echo);
    RUN_TEST(test_set_shell);
    RUN_TEST(test_factory_reset);
    RUN_TEST(test_selftest_reflects_bridge);
    RUN_TEST(test_bootsel_and_reset);
    RUN_TEST(test_unknown_and_errors);
    return UNITY_END();
}
