# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: Apache-2.0

# Host-side unit tests for DUTler's pure logic (no Pico SDK, native compiler).
# Build & run:  cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.13)
project(dutler_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_executable(unit
    test_main.c
    ../src/util/crc32.c
    ../src/util/numparse.c
    ../src/settings_codec.c
)
target_include_directories(unit PRIVATE ../src ../include)

# -Werror and sanitizers are opt-in so local `cmake -S tests` stays friendly;
# CI turns both on (see .github/workflows/ci.yml).
option(DUTLER_WERROR "Treat compiler warnings as errors" OFF)
option(DUTLER_SANITIZE "Build the tests with AddressSanitizer + UBSan" OFF)

if(NOT MSVC)
    set(test_warnings -Wall -Wextra -Wshadow -Wdouble-promotion -Wconversion)
    if(DUTLER_WERROR)
        list(APPEND test_warnings -Werror)
    endif()
    target_compile_options(unit PRIVATE ${test_warnings})
endif()

if(DUTLER_SANITIZE)
    target_compile_options(unit PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(unit PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME unit COMMAND unit)
