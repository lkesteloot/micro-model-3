cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(mini-model-3 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")
pico_sdk_init()

add_executable(mini-model-3
    main.c
    z80emu.c
    fonts.cpp
    trs80.cpp
    model3_rom.c
    ili9341.c
    gfx.c
)

# Serial output via USB:
pico_enable_stdio_usb(mini-model-3 1)
# Serial output via UART:
pico_enable_stdio_uart(mini-model-3 0)

pico_add_extra_outputs(mini-model-3)
target_link_libraries(mini-model-3 pico_stdlib hardware_spi hardware_dma)
