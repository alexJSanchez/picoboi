// CRC-32 (IEEE 802.3, the one used by zip, zlib and Python's zlib.crc32). Small and table-light
// (16 entries) so it costs little flash; pure logic, host-tested.
#ifndef PB_CRC32_H
#define PB_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Continues a CRC: start with crc = 0, feed chunks in order, the result of each call is valid
// on its own. pb_crc32_update(pb_crc32_update(0, a, na), b, nb) == pb_crc32(a followed by b).
uint32_t pb_crc32_update(uint32_t crc, const void *data, size_t len);

static inline uint32_t pb_crc32(const void *data, size_t len) { return pb_crc32_update(0, data, len); }

#ifdef __cplusplus
}
#endif

#endif
