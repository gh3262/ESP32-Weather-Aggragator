#pragma once

#include <cstring>
#include <cassert>
#include <map>
#include <string>

namespace fake_preferences {
inline std::map<std::string, std::string>& storage() {
  static std::map<std::string, std::string> values;
  return values;
}
extern bool failOpen;
extern bool failWrite;
extern unsigned writes;
}

struct Preferences {
  bool begin(const char* name, bool readOnly) {
    assert(std::string(name) == "omp-history" && !readOnly);
    return !fake_preferences::failOpen;
  }
  size_t getBytesLength(const char* key) {
    return fake_preferences::storage()[key].size();
  }
  size_t getBytes(const char* key, void* output, size_t count) {
    const std::string& value = fake_preferences::storage()[key];
    if (value.size() != count) return 0;
    memcpy(output, value.data(), count);
    return count;
  }
  size_t putBytes(const char* key, const void* input, size_t count) {
    if (fake_preferences::failWrite) return 0;
    ++fake_preferences::writes;
    fake_preferences::storage()[key].assign(static_cast<const char*>(input), count);
    return count;
  }
};
