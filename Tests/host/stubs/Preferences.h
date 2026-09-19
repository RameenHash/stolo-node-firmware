#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
class Preferences {
 public:
  using Data = std::map<std::string, std::vector<uint8_t>>;
  Data data;
  std::vector<Data> snapshots;
  std::vector<std::string> writes;
  bool fail_writes = false, fail_begin = false;
  std::string fail_key, fail_read_key, fail_length_key;
  int fail_call = -1, write_count = 0;
  void end() {}
  bool begin(const char*, bool) { return !fail_begin; }
  bool isKey(const char* key) { return data.count(key); }
  size_t getBytesLength(const char* key) { return fail_length_key == key || !isKey(key) ? 0 : data[key].size(); }
  bool writable(const char* key) { writes.push_back(key); ++write_count; return !fail_writes && fail_key != key && write_count != fail_call; }
  size_t put(const char* key, const void* p, size_t n) { if (!writable(key)) return 0; auto b = (const uint8_t*)p; data[key] = {b, b+n}; snapshots.push_back(data); return n; }
  uint8_t getUChar(const char* key, uint8_t fallback = 0) { return fail_read_key != key && data.count(key) && data[key].size()==1 ? data[key][0] : fallback; }
  size_t putUChar(const char* key, uint8_t v) { return put(key,&v,1); }
  uint32_t getUInt(const char* key, uint32_t fallback = 0) { if (fail_read_key == key || !data.count(key) || data[key].size() != 4) return fallback; uint32_t v; memcpy(&v, data[key].data(), 4); return v; }
  size_t putUInt(const char* key, uint32_t v) { return put(key,&v,4); }
  size_t putBytes(const char* key, const void* p, size_t n) { return put(key,p,n); }
  size_t getBytes(const char* key, void* p, size_t n) { if (fail_read_key == key || !data.count(key) || data[key].size() > n) return 0; memcpy(p, data[key].data(), data[key].size()); return data[key].size(); }
  bool remove(const char* key) { if (!writable(key)) return false; data.erase(key); snapshots.push_back(data); return true; }
  bool clear() { if (!writable("clear")) return false; data.clear(); snapshots.push_back(data); return true; }
};
