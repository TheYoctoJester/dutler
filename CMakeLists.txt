# SPDX-FileCopyrightText: 2026 Northern.tech AS
# SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Northern.tech-Commercial

cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # build/compile_commands.json for clangd/editors

# Target a stock Raspberry Pi Pico (RP2040).
set(PICO_BOARD pico CACHE STRING "Target board")

# Pull in the Pico SDK (uses $PICO_SDK_PATH).
include(pico_sdk_import.cmake)

project(dutler C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Firmware version from git: a tag like "v0.1.0" once tagged, otherwise the short
# commit (with "-dirty" if the tree has uncommitted changes). Re-derived when the
# git HEAD/index changes so the embedded version stays honest.
set(DUTLER_VERSION "unknown")
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty --match "v*"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        OUTPUT_VARIABLE DUTLER_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT DUTLER_VERSION)
        set(DUTLER_VERSION "unknown")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/.git/HEAD"
        "${CMAKE_CURRENT_LIST_DIR}/.git/index")
endif()
message(STATUS "DUTler version: ${DUTLER_VERSION}")

pico_sdk_init()

# Developed against Pico SDK 2.2.0; require the 2.x line (auto-picotool, etc.).
if(PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR
        "DUTler needs Pico SDK >= 2.0.0 (found ${PICO_SDK_VERSION_STRING}); "
        "developed against 2.2.0")
endif()

add_executable(dutler
    src/main.c
    src/usb_descriptors.c
    src/bridge.c
    src/relay.c
    src/debug.c
    src/settings.c
    src/settings_codec.c
    src/crc32.c
    src/parse.c
)

target_include_directories(dutler PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src      # tusb_config.h lives here
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Warnings on our own sources only (PRIVATE — not the Pico SDK / TinyUSB libs).
target_compile_options(dutler PRIVATE -Wall -Wextra)

# This firmware does no floating-point. Drop printf's float/exponential support
# so the double-precision soft-float runtime isn't dragged into the image.
target_compile_definitions(dutler PRIVATE
    PICO_PRINTF_SUPPORT_FLOAT=0
    PICO_PRINTF_SUPPORT_EXPONENTIAL=0
    DUTLER_VERSION="${DUTLER_VERSION}"
)

target_link_libraries(dutler
    pico_stdlib
    tinyusb_device
    hardware_uart
    hardware_irq
    hardware_flash
    pico_unique_id
)

# Binary metadata — shown by `picotool info`.
pico_set_program_name(dutler "DUTler")
pico_set_program_version(dutler "${DUTLER_VERSION}")
pico_set_program_description(dutler "USB-UART bridge + relay/strap/reset control for a DUT")
pico_set_program_url(dutler "https://github.com/TheYoctoJester/dutler")

# Emit .uf2 / .bin / .hex / map alongside the .elf.
pico_add_extra_outputs(dutler)
