// Embeds a test ROM into the firmware's read-only data (flash, read in place via XIP).
// The ROM file is never committed; CMake passes its path (see CMakeLists.txt).
    .section .rodata.embedded_rom, "a"
    .balign 4
    .global pb_embedded_rom
    .global pb_embedded_rom_end
pb_embedded_rom:
    .incbin PB_EMBED_ROM_PATH
pb_embedded_rom_end:
