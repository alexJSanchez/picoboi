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
  platform/ui.cpp
  launcher/launcher.cpp
  launcher/launcher_model.c
  storage/crc32.c
  storage/rom_library.c
  core/pb_core.c
  core/pb_pacer.c
  core/pb_audio_sync.c
  third_party/peanut_gb/examples/sdl2/minigb_apu/minigb_apu.c
)
target_include_directories(picoboy PRIVATE core platform launcher storage app third_party/peanut_gb
  third_party/peanut_gb/examples/sdl2/minigb_apu)
target_compile_definitions(picoboy PRIVATE MINIGB_APU_AUDIO_FORMAT_S16SYS=1)
# Saving the games' battery RAM to flash (ON by default: the author wants in-game saves to survive
# power-off). -DPB_SAVES=OFF builds a firmware with no save code at all and nothing that can write
# to flash (see docs/DECISIONS.md).
option(PB_SAVES "Save the games' battery RAM to flash" ON)
if(PB_SAVES)
  target_sources(picoboy PRIVATE storage/save_store.c storage/save_policy.c platform/flash_ops.cpp app/save_manager.cpp)
  target_link_libraries(picoboy pico_flash hardware_flash)
  target_compile_definitions(picoboy PRIVATE PB_SAVES)
  # flash_safe_execute(): we never start the second core, so tell the SDK it may assume core 1 is safe
  # (otherwise it refuses to run). Revisit if core 1 is ever used.
  target_compile_definitions(picoboy PRIVATE PICO_FLASH_ASSUME_CORE1_SAFE=1)
  # The default board is a 2 MiB Pico but the PicoSystem has 16 MiB and the save regions are at 10 MiB.
  # The SDK's flash_range_erase/program halt the program (interrupts off) for offsets beyond
  # PICO_FLASH_SIZE_BYTES, so it must say 16 MiB; platform/flash_ops.cpp checks this at build time.
  target_compile_definitions(picoboy PRIVATE PICO_FLASH_SIZE_BYTES=16777216)
endif()

# Development only: -DPB_EMBED_ROM=<file> builds a ROM into the firmware (read in place via XIP) and
# runs it directly, skipping the game list; used for benchmarks and the tone build. The ROM is
# never committed. By default nothing is embedded and the firmware boots to the game list.
set(PB_EMBED_ROM "" CACHE FILEPATH "Development only: ROM to build into the firmware and run directly, skipping the game list")
if(PB_EMBED_ROM AND EXISTS ${PB_EMBED_ROM})
  target_sources(picoboy PRIVATE src/embedded_rom.S)
  set_source_files_properties(src/embedded_rom.S PROPERTIES
    COMPILE_DEFINITIONS "PB_EMBED_ROM_PATH=\"${PB_EMBED_ROM}\""
    OBJECT_DEPENDS ${PB_EMBED_ROM})
  target_compile_definitions(picoboy PRIVATE PB_HAVE_EMBEDDED_ROM)
  message(STATUS "Embedding ROM: ${PB_EMBED_ROM}")
else()
  message(STATUS "No embedded ROM: this firmware boots to the game list (library from tools/pack_library.py)")
endif()

# Own copy of the SDK's linker script (ld/picoboy_memmap.ld.in): runs the emulator code from RAM and
# limits the firmware's flash size. The limit comes from storage/flash_layout.h, the single source
# of truth: the normal firmware must stay inside PB_FW_SIZE so it can never overwrite the library
# index and ROM area; a development build with an embedded ROM exceeds that on purpose and gets
# the whole usable flash (PB_FLASH_USER_END). -DPB_FLASH_LIMIT_KB=<n> overrides (used to test the cap).
file(READ ${CMAKE_CURRENT_LIST_DIR}/storage/flash_layout.h PB_LAYOUT_TEXT)
string(REGEX MATCH "#define PB_FW_SIZE (0x[0-9A-Fa-f]+)u" _unused "${PB_LAYOUT_TEXT}")
set(PB_FW_SIZE_HEX "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define PB_FLASH_USER_END (0x[0-9A-Fa-f]+)u" _unused "${PB_LAYOUT_TEXT}")
set(PB_USER_END_HEX "${CMAKE_MATCH_1}")
if(NOT PB_FW_SIZE_HEX OR NOT PB_USER_END_HEX)
  message(FATAL_ERROR "cannot read PB_FW_SIZE / PB_FLASH_USER_END from storage/flash_layout.h")
endif()
set(PB_FLASH_LIMIT_KB "" CACHE STRING "Override the firmware flash limit in KiB (testing only)")
if(NOT PB_FLASH_LIMIT_KB)
  if(PB_EMBED_ROM AND EXISTS ${PB_EMBED_ROM})
    math(EXPR PB_FLASH_LIMIT_KB_USED "${PB_USER_END_HEX} / 1024")
  else()
    math(EXPR PB_FLASH_LIMIT_KB_USED "${PB_FW_SIZE_HEX} / 1024")
  endif()
else()
  set(PB_FLASH_LIMIT_KB_USED ${PB_FLASH_LIMIT_KB})
endif()
message(STATUS "Firmware flash limit: ${PB_FLASH_LIMIT_KB_USED} KiB")
set(PB_FLASH_LIMIT_KB ${PB_FLASH_LIMIT_KB_USED})  # value substituted into the linker script template
configure_file(${CMAKE_CURRENT_LIST_DIR}/ld/picoboy_memmap.ld.in ${CMAKE_CURRENT_BINARY_DIR}/picoboy_memmap.ld @ONLY)
pico_set_linker_script(picoboy ${CMAKE_CURRENT_BINARY_DIR}/picoboy_memmap.ld)


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

# Boot benchmark: before starting the game, run its first frames three ways and print the
# timings over USB serial (needs a serial reader connected). See src/main.cpp.
option(PB_BOOT_BENCH "Run the boot benchmark at start-up" OFF)
if(PB_BOOT_BENCH)
  target_compile_definitions(picoboy PRIVATE PB_BOOT_BENCH)
endif()

# Peanut-GB accuracy option for sprite rendering (see third_party/peanut_gb/peanut_gb.h).
set(PB_HIGH_LCD_ACCURACY 1 CACHE STRING "PEANUT_GB_HIGH_LCD_ACCURACY (0 or 1)")
target_compile_definitions(picoboy PRIVATE PEANUT_GB_HIGH_LCD_ACCURACY=${PB_HIGH_LCD_ACCURACY})

# Flash test build: no emulator; inspects the flash and writes a marker into the settings area,
# to verify the library UF2 approach on hardware (docs/HARDWARE_TESTS.md).
option(PB_FLASH_TEST "Build the flash test firmware" OFF)
if(PB_FLASH_TEST)
  target_sources(picoboy PRIVATE src/flash_test.cpp storage/crc32.c storage/rom_library.c)
  target_include_directories(picoboy PRIVATE storage src)
  target_link_libraries(picoboy pico_flash hardware_flash)
  # We never start the second core, so flash_safe_execute may assume it is safe.
  target_compile_definitions(picoboy PRIVATE PB_FLASH_TEST PICO_FLASH_ASSUME_CORE1_SAFE=1 PICO_FLASH_SIZE_BYTES=16777216)
endif()
