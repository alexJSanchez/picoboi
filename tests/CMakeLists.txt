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
