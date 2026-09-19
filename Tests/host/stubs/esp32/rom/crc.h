#pragma once
#include <cstdint>
#include <cstddef>
inline uint32_t crc32_le(uint32_t crc, const uint8_t* p, size_t n) {
  crc = ~crc;
  for (size_t i = 0; i < n; i++) { crc ^= p[i]; for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1))); }
  return ~crc;
}
