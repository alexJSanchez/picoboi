// Flash test mode (CMake option PB_FLASH_TEST). The firmware does not run the emulator; it
// inspects the flash and reports, so the library UF2 approach can be verified on hardware
// (docs/HARDWARE_TESTS.md, "Phase 3: flash and UF2"). Test-only code.
#ifndef PB_FLASH_TEST_H
#define PB_FLASH_TEST_H

// Runs all checks once (and writes the settings marker if the area is blank). Call from init().
void flash_test_init();
// Prints the report over USB serial every few seconds. Call from update().
void flash_test_update();
// Shows a summary on the screen. Call from draw().
void flash_test_draw();

#endif
