# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: Apache-2.0

# Host unit tests for DUTler (no Pico SDK, native compiler + Unity).
# Build & run:
#   cmake -S tests -B build-tests -G Ninja [-DDUTLER_WERROR=ON -DDUTLER_SANITIZE=ON]
#   cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
cmake_minimum_required(VERSION 3.13)
project(dutler_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

# Match the firmware build's opt-in knobs. -Wconversion/-Wshadow stay enforced on
# the production sources by the firmware build; here we keep -Wall -Wextra so the
# vendored Unity macros don't trip the stricter set.
option(DUTLER_WERROR "Treat compiler warnings as errors (our code)" OFF)
option(DUTLER_SANITIZE "Build the tests with AddressSanitizer + UBSan" OFF)

set(SAN_C "")
set(SAN_L "")
if(DUTLER_SANITIZE)
    set(SAN_C -fsanitize=address,undefined -fno-omit-frame-pointer)
    set(SAN_L -fsanitize=address,undefined)
endif()

set(OUR_WARN -Wall -Wextra)
if(DUTLER_WERROR)
    list(APPEND OUR_WARN -Werror)
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(INC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Vendored Unity (third-party) — compiled plain, never with -Werror/strict warnings.
add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)
target_compile_options(unity PRIVATE ${SAN_C})

# Define a test executable from our sources + register it with CTest.
function(dutler_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${SRC} ${INC})
    target_link_libraries(${name} PRIVATE unity)
    target_compile_options(${name} PRIVATE ${OUR_WARN} ${SAN_C})
    target_link_options(${name} PRIVATE ${SAN_L})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Pure logic — no fakes needed.
dutler_test(test_pure
    test_pure.c
    ${SRC}/util/crc32.c ${SRC}/util/numparse.c ${SRC}/core/settings_codec.c)

# Interactive line editor — pure logic, driven by a captured write callback.
dutler_test(test_lineedit
    test_lineedit.c
    ${SRC}/util/lineedit.c)

# Persistence — real settings.c against the RAM flash fake.
dutler_test(test_settings
    test_settings.c
    ${SRC}/core/settings.c ${SRC}/core/settings_codec.c ${SRC}/util/crc32.c
    fakes/flash_port_fake.c)
target_include_directories(test_settings PRIVATE fakes)

# Command interpreter — real command.c/outputs.c/settings.c + fakes + SDK shims.
dutler_test(test_command
    test_command.c
    ${SRC}/core/command.c ${SRC}/core/outputs.c ${SRC}/core/settings.c ${SRC}/core/settings_codec.c
    ${SRC}/util/crc32.c ${SRC}/util/numparse.c
    fakes/flash_port_fake.c fakes/fake_console.c fakes/fake_bridge.c
    fakes/fake_debug.c fakes/fake_sdk.c)
target_include_directories(test_command PRIVATE fakes shims)
