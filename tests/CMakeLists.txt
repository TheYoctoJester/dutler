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
    ../src/crc32.c
    ../src/parse.c
    ../src/settings_codec.c
)
target_include_directories(unit PRIVATE ../src ../include)
if(NOT MSVC)
    target_compile_options(unit PRIVATE -Wall -Wextra)
endif()

add_test(NAME unit COMMAND unit)
