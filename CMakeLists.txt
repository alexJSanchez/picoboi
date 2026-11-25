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
  platform/audio.cpp
  platform/display.cpp
  platform/input.cpp
  core/pb_core.c
  core/pb_pacer.c
  core/pb_audio_sync.c
  third_party/peanut_gb/examples/sdl2/minigb_apu/minigb_apu.c
)
target_include_directories(picoboy PRIVATE core platform third_party/peanut_gb
  third_party/peanut_gb/examples/sdl2/minigb_apu)
target_compile_definitions(picoboy PRIVATE MINIGB_APU_AUDIO_FORMAT_S16SYS=1)

# Phase 1: embed a free test ROM (read in place via XIP). The ROM is downloaded by
# tools/fetch_test_roms.sh into the untracked tests/roms/ and is never committed.
# Without it the firmware still builds and shows a "no ROM embedded" message.
set(PB_EMBED_ROM ${CMAKE_CURRENT_LIST_DIR}/tests/roms/cpu_instrs.gb CACHE FILEPATH "ROM to embed (Phase 1 only)")
if(EXISTS ${PB_EMBED_ROM})
  target_sources(picoboy PRIVATE src/embedded_rom.S)
  set_source_files_properties(src/embedded_rom.S PROPERTIES
    COMPILE_DEFINITIONS "PB_EMBED_ROM_PATH=\"${PB_EMBED_ROM}\""
    OBJECT_DEPENDS ${PB_EMBED_ROM})
  target_compile_definitions(picoboy PRIVATE PB_HAVE_EMBEDDED_ROM)
  message(STATUS "Embedding ROM: ${PB_EMBED_ROM}")
else()
  message(STATUS "No ROM to embed (run tools/fetch_test_roms.sh); building without one")
endif()


# Phase 0: keep the SDK defaults (240x240, overclock on). Revisit in Phase 2.
disable_startup_logo(picoboy)

# USB serial logging (PLAN.md [VERIFY] #7). No UART pins are exposed on the PicoSystem.
pico_enable_stdio_usb(picoboy 1)
pico_enable_stdio_uart(picoboy 0)

# Audio test build: plays steady test tones (several pitches) instead of game audio, to judge
# the piezo speaker on its own.  cmake -DPB_AUDIO_TEST_TONE=ON ...
option(PB_AUDIO_TEST_TONE "Play test tones instead of game audio" OFF)
if(PB_AUDIO_TEST_TONE)
  target_compile_definitions(picoboy PRIVATE PB_AUDIO_TEST_TONE)
endif()
