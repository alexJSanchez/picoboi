#include "flash_ops.h"

#include "hardware/flash.h"
#include "pico/flash.h"

#include "flash_layout.h"

// The SDK's flash_range_erase/program contain an always-on check against PICO_FLASH_SIZE_BYTES that
// panics (a silent hang with interrupts off) for offsets beyond it. The default board is a 2 MiB
// Pico, but the PicoSystem has 16 MiB and our save regions are at 10 MiB, so the build must say so
// (CMakeLists.txt). This makes a wrong value a build error and not a frozen device.
static_assert(PICO_FLASH_SIZE_BYTES >= PB_FLASH_USER_END,
              "PICO_FLASH_SIZE_BYTES is smaller than our flash regions: flash writes would hang");

namespace {

struct Job {
  uint32_t offset;
  const uint8_t *data;
  uint32_t len;
};

// These run with interrupts off and the flash not readable, so they must live in RAM and touch
// only RAM (the SDK's flash_range_* functions are already in RAM).
void __no_inline_not_in_flash_func(do_erase)(void *p) {
  const Job *j = static_cast<const Job *>(p);
  flash_range_erase(j->offset, j->len);
}

void __no_inline_not_in_flash_func(do_program)(void *p) {
  const Job *j = static_cast<const Job *>(p);
  flash_range_program(j->offset, j->data, j->len);
}

constexpr uint32_t SAFE_EXECUTE_TIMEOUT_MS = 1000;

const uint8_t *op_read(void *, uint32_t offset) {
  return reinterpret_cast<const uint8_t *>(PB_FLASH_XIP_BASE + offset);
}

bool op_erase(void *, uint32_t offset, uint32_t len) {
  if (!pb_flash_erase_allowed(offset, len)) return false;  // second line of defence
  for (uint32_t done = 0; done < len; done += PB_FLASH_SECTOR_SIZE) {
    Job j{offset + done, nullptr, PB_FLASH_SECTOR_SIZE};
    if (flash_safe_execute(do_erase, &j, SAFE_EXECUTE_TIMEOUT_MS) != PICO_OK) return false;
  }
  return true;
}

bool op_program(void *, uint32_t offset, const uint8_t *data, uint32_t len) {
  if (!pb_flash_program_allowed(offset, len)) return false;
  // `data` must be in RAM: flash cannot be read while it is being programmed.
  for (uint32_t done = 0; done < len; done += PB_FLASH_SECTOR_SIZE) {
    const uint32_t n = (len - done < PB_FLASH_SECTOR_SIZE) ? len - done : PB_FLASH_SECTOR_SIZE;
    Job j{offset + done, data + done, n};
    if (flash_safe_execute(do_program, &j, SAFE_EXECUTE_TIMEOUT_MS) != PICO_OK) return false;
  }
  return true;
}

}  // namespace

pb_flash_ops_t flash_ops_device() {
  pb_flash_ops_t ops = {};
  ops.read = op_read;
  ops.erase = op_erase;
  ops.program = op_program;
  ops.ctx = nullptr;
  return ops;
}
