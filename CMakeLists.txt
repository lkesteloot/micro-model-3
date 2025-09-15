cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_FETCH_FROM_GIT ON)
set(PICO_SDK_FETCH_FROM_GIT_TAG 2.2.0)
set(PICO_BOARD pico)
set(PICO_PLATFORM rp2040)

include(src/cmake/pico_sdk_import.cmake)

project(micro-model-3 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")
pico_sdk_init()

add_executable(micro-model-3
    src/micro-model-3/main.cpp
    src/micro-model-3/trs80.cpp
    src/micro-model-3/ili9341.c
    src/micro-model-3/fonts.cpp
    src/z80emu/z80emu.c
    src/generated/model3_rom.c
    src/generated/obstacle_run_cmd.c
    src/generated/scarfman2_cmd.c
    src/generated/splash.cpp
)

target_include_directories(micro-model-3
    PRIVATE
        src/generated
        src/z80emu
)

# Serial output via USB:
pico_enable_stdio_usb(micro-model-3 1)

# Serial output via UART:
pico_enable_stdio_uart(micro-model-3 0)

# Generate various other output files, including the .uf2 file we need:
pico_add_extra_outputs(micro-model-3)

target_link_libraries(micro-model-3 pico_stdlib hardware_spi hardware_dma)
