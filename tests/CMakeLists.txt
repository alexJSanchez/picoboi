cmake_minimum_required(VERSION 3.13)
project(picoboy_host C)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(ROMS ${CMAKE_CURRENT_LIST_DIR}/roms)  # filled by tools/fetch_test_roms.sh (untracked)

set(APU_DIR ${ROOT}/third_party/peanut_gb/examples/sdl2/minigb_apu)
set(CORE_SOURCES ${ROOT}/core/pb_core.c ${APU_DIR}/minigb_apu.c)
add_compile_definitions(MINIGB_APU_AUDIO_FORMAT_S16SYS=1)

add_executable(core_run core_run.c ${CORE_SOURCES})
target_include_directories(core_run PRIVATE ${ROOT}/core ${ROOT}/third_party/peanut_gb ${APU_DIR})
target_compile_options(core_run PRIVATE -Wall -Wextra)

# Blargg cpu_instrs prints "Passed all tests" over serial. ~3300 frames at full speed.
add_test(NAME cpu_instrs
         COMMAND core_run ${ROMS}/cpu_instrs.gb 8000 --expect "Passed all tests")

# dmg-acid2: only checks it runs cleanly; the picture itself is checked by eye
# (open the dumped BMP and compare with the reference image).
add_test(NAME dmg_acid2_runs
         COMMAND core_run ${ROMS}/dmg-acid2.gb 200 --dump ${CMAKE_CURRENT_BINARY_DIR}/dmg-acid2.bmp)

# Same ROM with only every 3rd frame rendered (the 40 Hz panel case): emulation must be
# unaffected and the skipped frames must not draw.
add_test(NAME cpu_instrs_render_every_3
         COMMAND core_run ${ROMS}/cpu_instrs.gb 8000 --expect "Passed all tests" --render-every 3)

set_tests_properties(cpu_instrs dmg_acid2_runs cpu_instrs_render_every_3 PROPERTIES SKIP_RETURN_CODE 77)

# Pure-logic unit tests (no ROMs needed).
add_executable(test_pacer test_pacer.c ${ROOT}/core/pb_pacer.c)
target_include_directories(test_pacer PRIVATE ${ROOT}/core)
target_compile_options(test_pacer PRIVATE -Wall -Wextra)
add_test(NAME pacer COMMAND test_pacer)

# Audio: generated tone ROM, checks sample count per frame and pitch.
add_executable(test_audio test_audio.c ${CORE_SOURCES})
target_include_directories(test_audio PRIVATE ${ROOT}/core ${ROOT}/third_party/peanut_gb ${APU_DIR})
target_compile_options(test_audio PRIVATE -Wall -Wextra)
add_test(NAME audio_tone COMMAND test_audio)

# Audio producer/consumer synchronisation (pure logic).
add_executable(test_audio_sync test_audio_sync.c ${ROOT}/core/pb_audio_sync.c)
target_include_directories(test_audio_sync PRIVATE ${ROOT}/core)
target_compile_options(test_audio_sync PRIVATE -Wall -Wextra)
add_test(NAME audio_sync COMMAND test_audio_sync)

# Flash layout: region guards and the write-allowed checks.
add_executable(test_flash_layout test_flash_layout.c)
target_include_directories(test_flash_layout PRIVATE ${ROOT}/storage)
target_compile_options(test_flash_layout PRIVATE -Wall -Wextra)
add_test(NAME flash_layout COMMAND test_flash_layout)

# ROM library index: CRC-32 and the reader, checked against the golden file made by the PC tool.
add_executable(test_rom_library test_rom_library.c ${ROOT}/storage/crc32.c ${ROOT}/storage/rom_library.c)
target_include_directories(test_rom_library PRIVATE ${ROOT}/storage)
target_compile_options(test_rom_library PRIVATE -Wall -Wextra)
target_compile_definitions(test_rom_library PRIVATE GOLDEN_PATH="${CMAKE_CURRENT_LIST_DIR}/golden/index_v1.bin")
add_test(NAME rom_library COMMAND test_rom_library)

# The PC packing tool (needs Python 3; skipped with a message if it is not installed).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME pack_library COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/test_pack_library.py)
else()
  message(STATUS "Python 3 not found: pack_library tests are not registered")
endif()

# Launcher list logic (selection, scrolling, labels).
add_executable(test_launcher_model test_launcher_model.c ${ROOT}/launcher/launcher_model.c)
target_include_directories(test_launcher_model PRIVATE ${ROOT}/launcher ${ROOT}/storage)
target_compile_options(test_launcher_model PRIVATE -Wall -Wextra)
add_test(NAME launcher_model COMMAND test_launcher_model)

# Save store: power-loss safe saves against a simulated flash (cuts the power at every operation).
add_executable(test_save_store test_save_store.c ${ROOT}/storage/save_store.c ${ROOT}/storage/crc32.c)
target_include_directories(test_save_store PRIVATE ${ROOT}/storage)
target_compile_options(test_save_store PRIVATE -Wall -Wextra)
add_test(NAME save_store COMMAND test_save_store)

# Cart RAM change tracking in the core.
add_executable(test_core_ram test_core_ram.c ${CORE_SOURCES})
target_include_directories(test_core_ram PRIVATE ${ROOT}/core ${ROOT}/third_party/peanut_gb ${APU_DIR})
target_compile_options(test_core_ram PRIVATE -Wall -Wextra)
add_test(NAME core_ram COMMAND test_core_ram)

# When to save: idle and maximum-age rules.
add_executable(test_save_policy test_save_policy.c ${ROOT}/storage/save_policy.c)
target_include_directories(test_save_policy PRIVATE ${ROOT}/storage)
target_compile_options(test_save_policy PRIVATE -Wall -Wextra)
add_test(NAME save_policy COMMAND test_save_policy)

# Developer tool for the compatibility pass (not a test): runs a ROM with a button script and takes screenshots.
add_executable(play_rom play_rom.c ${CORE_SOURCES})
target_include_directories(play_rom PRIVATE ${ROOT}/core ${ROOT}/third_party/peanut_gb ${APU_DIR})
target_compile_options(play_rom PRIVATE -Wall -Wextra)
