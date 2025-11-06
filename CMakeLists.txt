cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_FETCH_FROM_GIT ON)
set(PICO_SDK_FETCH_FROM_GIT_TAG 2.2.0)
set(PICO_BOARD pico_w)

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
    src/micro-model-3/webapp.cpp
    src/z80emu/z80emu.c
    src/generated/model3_rom.c
    src/generated/obstacle_run_cmd.c
    src/generated/scarfman2_cmd.c
    src/generated/defense_command_cmd.c
    src/generated/sea_dragon_cmd.c
    src/generated/breakdown_cmd.c
    src/generated/ever_given_cmd.c
    src/generated/galaxy_invasion_cmd.c
    src/generated/splash.cpp
    src/generated/logos.c
)

target_include_directories(micro-model-3
    PRIVATE
        src/generated
        src/z80emu
        src/micro-model-3
        # ${PICO_LWIP_CONTRIB_PATH}/apps/httpd
)

target_compile_definitions(micro-model-3 PRIVATE
        WIFI_SSID=\"XXX\"
        WIFI_PASSWORD=\"XXX\"
        )

# Serial output via USB:
pico_enable_stdio_usb(micro-model-3 1)

# Generate various other output files, including the .uf2 file we need:
pico_add_extra_outputs(micro-model-3)

pico_add_library(pico_httpd_content NOFLAG)
pico_set_lwip_httpd_content(pico_httpd_content INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/web/404.html
    ${CMAKE_CURRENT_LIST_DIR}/src/web/index.shtml
    )

target_link_libraries(micro-model-3
    pico_stdlib
    pico_rand
    hardware_spi
    hardware_dma
    pico_cyw43_arch_lwip_poll
    pico_lwip_http
    pico_lwip_mdns
    pico_httpd_content
    )
