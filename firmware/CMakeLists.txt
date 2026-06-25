cmake_minimum_required(VERSION 3.13)

# Must be included before project()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(usb_wake_switch C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(usb_wake_switch
    src/main.c
    src/usb_host.c
    src/power_ctrl.c
)

target_include_directories(usb_wake_switch PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# TinyUSB host mode is enabled via tusb_config.h (CFG_TUSB_RHPORT0_MODE = OPT_MODE_HOST)
target_link_libraries(usb_wake_switch
    pico_stdlib
    tinyusb_host
    tinyusb_board
    hardware_gpio
    hardware_timer
)

# UART for debug output; do NOT enable stdio USB — the USB port is used as host
pico_enable_stdio_uart(usb_wake_switch 1)
pico_enable_stdio_usb(usb_wake_switch 0)

pico_add_extra_outputs(usb_wake_switch)   # generates .uf2, .bin, .hex, .map
