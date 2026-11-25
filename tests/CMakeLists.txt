cmake_minimum_required(VERSION 3.13)
project(picoboy_host C)
set(CMAKE_C_STANDARD 11)

enable_testing()

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(ROMS ${CMAKE_CURRENT_LIST_DIR}/roms)  # filled by tools/fetch_test_roms.sh (untracked)

add_executable(core_run core_run.c ${ROOT}/core/pb_core.c)
target_include_directories(core_run PRIVATE ${ROOT}/core ${ROOT}/third_party/peanut_gb)
target_compile_options(core_run PRIVATE -Wall -Wextra)

# Blargg cpu_instrs prints "Passed all tests" over serial. ~3300 frames at full speed.
add_test(NAME cpu_instrs
         COMMAND core_run ${ROMS}/cpu_instrs.gb 8000 --expect "Passed all tests")

# dmg-acid2: only checks it runs cleanly; the picture itself is checked by eye
# (open the dumped BMP and compare with the reference image).
add_test(NAME dmg_acid2_runs
         COMMAND core_run ${ROMS}/dmg-acid2.gb 200 --dump ${CMAKE_CURRENT_BINARY_DIR}/dmg-acid2.bmp)

set_tests_properties(cpu_instrs dmg_acid2_runs PROPERTIES SKIP_RETURN_CODE 77)
