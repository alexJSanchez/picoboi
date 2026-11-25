cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK (must be before project). Pass -DPICO_SDK_PATH=third_party/pico-sdk
if(NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk)
endif()
include(pico_sdk_import.cmake)

project(picoboy C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# PicoSystem SDK (pinned submodule, see docs/DECISIONS.md)
include(${CMAKE_CURRENT_LIST_DIR}/third_party/picosystem/libraries/picosystem.cmake)

picosystem_executable(picoboy
  src/main.cpp
  src/toolchain_shims.cpp
)


# Phase 0: keep the SDK defaults (240x240, overclock on). Revisit in Phase 2.
disable_startup_logo(picoboy)

# USB serial logging (PLAN.md [VERIFY] #7). No UART pins are exposed on the PicoSystem.
pico_enable_stdio_usb(picoboy 1)
pico_enable_stdio_uart(picoboy 0)
