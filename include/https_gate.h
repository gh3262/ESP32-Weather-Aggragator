#ifndef HTTPS_GATE_H
#define HTTPS_GATE_H

#include <atomic>

// Serializes REST HTTPS workers so only one extra TLS session (beyond MQTT)
// holds heap at a time. Callers that cannot acquire simply try again later.
namespace https_gate {

inline std::atomic<bool>& busyFlag() {
  static std::atomic<bool> busy(false);
  return busy;
}

inline bool tryAcquire() {
  bool expected = false;
  return busyFlag().compare_exchange_strong(expected, true);
}

inline void release() { busyFlag().store(false); }

}

#endif
