cmake_minimum_required(VERSION 3.13)

# Target a stock Raspberry Pi Pico (RP2040).
set(PICO_BOARD pico CACHE STRING "Target board")

# Pull in the Pico SDK (uses $PICO_SDK_PATH).
include(pico_sdk_import.cmake)

project(usb_uart_relay C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(usb_uart_relay
    src/main.c
    src/usb_descriptors.c
    src/bridge.c
    src/relay.c
    src/debug.c
    src/settings.c
)

target_include_directories(usb_uart_relay PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src      # tusb_config.h lives here
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# This firmware does no floating-point. Drop printf's float/exponential support
# so the double-precision soft-float runtime isn't dragged into the image.
target_compile_definitions(usb_uart_relay PRIVATE
    PICO_PRINTF_SUPPORT_FLOAT=0
    PICO_PRINTF_SUPPORT_EXPONENTIAL=0
)

target_link_libraries(usb_uart_relay
    pico_stdlib
    tinyusb_device
    hardware_uart
    hardware_irq
    hardware_flash
    pico_unique_id
)

# Emit .uf2 / .bin / .hex / map alongside the .elf.
pico_add_extra_outputs(usb_uart_relay)
