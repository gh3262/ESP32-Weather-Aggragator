#pragma once

#include <stdint.h>
#include <stddef.h>

namespace wifi_retry {
constexpr unsigned kFailureThreshold = 2;
constexpr uint32_t kCooldownMs = 10UL * 60UL * 1000UL;

struct NetworkRetry {
  unsigned failures = 0;
  bool coolingDown = false;
  uint32_t cooldownStartedAt = 0;

  bool eligible(uint32_t now) {
    if (coolingDown && now - cooldownStartedAt >= kCooldownMs) {
      reset();
    }
    return !coolingDown;
  }

  void failed(uint32_t now) {
    if (!coolingDown && ++failures >= kFailureThreshold) {
      coolingDown = true;
      cooldownStartedAt = now;
    }
  }

  void reset() {
    failures = 0;
    coolingDown = false;
  }
};

struct SavedNetwork {
  int32_t rssi = 0;
  bool visible = false;
  NetworkRetry retry;

  void observe(int32_t signal) {
    if (!visible || signal > rssi) {
      rssi = signal;
    }
    visible = true;
  }
};

inline size_t selectStrongest(SavedNetwork* networks, size_t count, uint32_t now) {
  size_t strongest = count;
  for (size_t i = 0; i < count; ++i) {
    if (networks[i].visible && networks[i].retry.eligible(now) &&
        (strongest == count || networks[i].rssi > networks[strongest].rssi)) {
      strongest = i;
    }
  }
  return strongest;
}
}  // namespace wifi_retry
