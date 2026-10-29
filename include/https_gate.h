#ifndef HTTPS_GATE_H
#define HTTPS_GATE_H

#include <atomic>

// Serializes REST HTTPS workers (OpenWeather, WeatherAPI, AIO rate check) so
// only one extra TLS session (beyond MQTT) holds heap at a time. Callers that
// cannot acquire simply try again later.
namespace https_gate {

inline std::atomic<bool>& busyFlag() {
  static std::atomic<bool> busy(false);
  return busy;
}

inline std::atomic<const char*>& ownerName() {
  static std::atomic<const char*> owner(nullptr);
  return owner;
}

// owner must be a string literal; it is reported by heap diagnostics.
inline bool tryAcquire(const char* owner) {
  bool expected = false;
  if (!busyFlag().compare_exchange_strong(expected, true)) {
    return false;
  }
  ownerName().store(owner);
  return true;
}

inline void release() {
  ownerName().store(nullptr);
  busyFlag().store(false);
}

inline const char* owner() {
  const char* name = ownerName().load();
  return name ? name : "-";
}

}

#endif
