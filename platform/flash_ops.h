// platform/flash_ops: the real flash for storage/save_store on the device. Reads are plain XIP
// reads; erase and program go through flash_safe_execute() (interrupts off, XIP paused) in
// small pieces so interrupts are never blocked for long (one 4 KiB sector erase is about 45 ms).
//
// Needs PICO_FLASH_ASSUME_CORE1_SAFE: we never start the second core (see CMakeLists.txt).
#ifndef PB_PLATFORM_FLASH_OPS_H
#define PB_PLATFORM_FLASH_OPS_H

#include "save_store.h"

pb_flash_ops_t flash_ops_device();

#endif
