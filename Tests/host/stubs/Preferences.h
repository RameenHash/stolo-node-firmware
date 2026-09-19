#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
class Preferences {
 public:
  std::map<std::string, std::vector<uint8_t>> data;
  bool fail_writes = false;
  void begin(const char*, bool) {}
  uint8_t getUChar(const char* key, uint8_t fallback = 0) { return data.count(key) ? data[key][0] : fallback; }
  size_t putUChar(const char* key, uint8_t v) { if (fail_writes) return 0; data[key] = {v}; return 1; }
  uint32_t getUInt(const char* key, uint32_t fallback = 0) { if (!data.count(key) || data[key].size() != 4) return fallback; uint32_t v; memcpy(&v, data[key].data(), 4); return v; }
  size_t putUInt(const char* key, uint32_t v) { if (fail_writes) return 0; auto b = (uint8_t*)&v; data[key] = {b, b + 4}; return 4; }
  size_t putBytes(const char* key, const void* p, size_t n) { if (fail_writes) return 0; auto b = (const uint8_t*)p; data[key] = {b, b + n}; return n; }
  size_t getBytes(const char* key, void* p, size_t n) { if (!data.count(key) || data[key].size() > n) return 0; memcpy(p, data[key].data(), data[key].size()); return data[key].size(); }
};
