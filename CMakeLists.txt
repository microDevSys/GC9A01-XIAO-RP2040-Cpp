# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD seeed_xiao_rp2040 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(main C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(main
        main.cpp
        TFT.cpp
        Ball.cpp
        ScrollableArea.cpp
        DHT11.cpp
        FAT32.cpp
        SDCard.cpp
        AnimationPlayer.cpp
        StorageManager.cpp
    rgb2.cpp
        )

target_link_libraries(main 
    pico_stdlib 
    hardware_spi 
    hardware_irq 
    hardware_gpio
    hardware_uart
) 

target_compile_definitions(main PRIVATE 
    PICO_HEAP_SIZE=20480        # 20KB heap 
    PICO_STACK_SIZE=3584        # 3.5KB stack 
    PICO_DEFAULT_UART_BAUD_RATE=115200
)

# create map/bin/hex file etc.
pico_add_extra_outputs(main)

# Activer la sortie stdio via USB (CDC) et UART (115200 bauds)
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)

## Removed sd_list auxiliary executable as test.cpp was deleted