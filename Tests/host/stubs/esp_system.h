#pragma once
#include <cstdint>
#include <cstring>
inline uint8_t esp_random_seed = 1;
inline void esp_fill_random(void* p, size_t n) { uint8_t* b = (uint8_t*)p; for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(esp_random_seed * 31 + i); esp_random_seed++; }
inline int esp_reset_reason() { return 1; }
