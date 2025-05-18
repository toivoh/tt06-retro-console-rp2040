cmake_minimum_required(VERSION 3.12)

# Change your executable name to something creative!
set(NAME tt-example) # <-- Name your project/executable here!

include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

add_compile_options(-Wall -Werror -Wno-error=unused-variable -Wno-error=unused-but-set-variable)

# Add your source files
add_executable(${NAME}
    main.cpp
    tt_setup.c
    serial-ram-emu.c
    serial-ram-emu.h
#    serial-ram-emu-main.c
    demo.cpp
    anemonegrafx8.cpp
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/sram_memmap.ld)
pico_add_link_depend(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/sram_memmap.ld)

pico_generate_pio_header(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/serial-ram-emu.pio)

# Don't forget to link the libraries you need!
target_link_libraries(${NAME}
    pico_stdlib
    hardware_gpio
    hardware_pio
    hardware_dma
    hardware_pwm
    pico_multicore
)

pico_enable_stdio_usb(${NAME} 1)
pico_enable_stdio_uart(${NAME} 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
