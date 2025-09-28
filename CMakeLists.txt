cmake_minimum_required(VERSION 3.28)

project(vitapopscompat VERSION 1.0 LANGUAGES C ASM)

set(DEBUG "" CACHE STRING "Debug level (1,2,3)")
message(STATUS "DEBUG LEVEL = ${DEBUG}")

include(FetchContent)
FetchContent_Declare(
    arkdevsdk
    GIT_REPOSITORY https://github.com/PSP-Arkfive/ark-dev-sdk.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(
    bootloadex
    GIT_REPOSITORY https://github.com/PSP-Arkfive/BootLoadEx.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(arkdevsdk bootloadex)

set(ARKSDK ${arkdevsdk_SOURCE_DIR})

add_prx_module(vitapopscompat exports.exp)

target_sources(vitapopscompat PRIVATE 
   main.c
   src/syspatch.c
   src/popsdisplay.c
   src/vitaflash.c
   imports.S
   payload.h
)

remove_definitions("-D_PSP_FW_VERSION=600")
add_definitions("-D_PSP_FW_VERSION=660")

target_compile_options(vitapopscompat PRIVATE -std=c99 -Os -G0 -Wall -fno-pic)
target_include_directories(vitapopscompat PRIVATE include ${ARKSDK}/include ${CMAKE_CURRENT_BINARY_DIR})
target_link_directories(vitapopscompat PRIVATE ${ARKSDK}/libs)
target_link_libraries(vitapopscompat PRIVATE
   -nostartfiles
   -nostdlib
   pspsystemctrl_kernel
   pspkermit_driver
   pspkermitperipheral_driver
   pspsdk
   pspmodinfo
   pspkernel
)

add_custom_command(OUTPUT payload.h
  DEPENDS vitapopscompat_reboot
  COMMAND bin2c "$<TARGET_FILE:vitapopscompat_reboot>.bin.gz" payload.h rebootbuffer_vitapops
)

add_subdirectory(rebootex)