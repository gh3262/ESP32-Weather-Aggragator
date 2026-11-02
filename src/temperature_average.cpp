#include "temperature_average.h"

#include <Arduino.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "aio_publisher.h"

namespace temperature_average {
namespace {
constexpr uint32_t kMaxAgeMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kRetryMs = 2000;

struct Input {
  const char* key;
  double value;
  uint32_t receivedAt;
  bool fresh;
};

Input inputs[] = {
    {"om-temp", 0, 0, false},
    {"currenttemperature", 0, 0, false},
    {"openweather.owtemp", 0, 0, false},
    {"wapi.temperature", 0, 0, false}};
char lastQueuedValue[32] = {};
bool retryPending = false;
uint32_t lastRetryAt = 0;
bool unavailableReported = true;
}

void begin() {
  for (Input& input : inputs) {
    input.value = 0;
    input.receivedAt = 0;
    input.fresh = false;
  }
  lastQueuedValue[0] = '\0';
  retryPending = false;
  lastRetryAt = 0;
  unavailableReported = true;
  Serial.println("Feed-Avg: enabled; averaging fresh current temperatures (30-minute cutoff)");
}

void observe(const char* key, const char* value, uint32_t receivedAt) {
  for (Input& input : inputs) {
    if (strcmp(input.key, key) != 0) {
      continue;
    }
    char* end = nullptr;
    errno = 0;
    const double temperature = value ? std::strtod(value, &end) : NAN;
    if (!value || end == value || *end != '\0' || errno == ERANGE ||
        !std::isfinite(temperature)) {
      Serial.printf("Feed-Avg ERROR: invalid temperature from %s; retaining previous sample\n",
                    key);
      return;
    }
    input.value = temperature;
    input.receivedAt = receivedAt;
    input.fresh = true;
    return;
  }
}

void update(uint32_t now) {
  size_t count = 0;
  for (Input& input : inputs) {
    // A MQTT sample can arrive a few ticks after the main loop captured now.
    // Signed elapsed time also handles millis() rollover; expired inputs stay
    // expired until a new sample arrives.
    if (input.fresh &&
        static_cast<int32_t>(now - input.receivedAt) >=
            static_cast<int32_t>(kMaxAgeMs)) {
      input.fresh = false;
    }
    if (input.fresh) {
      ++count;
    }
  }
  if (count == 0) {
    retryPending = false;
    if (!unavailableReported) {
      Serial.println("Feed-Avg: no fresh temperatures; leaving last published value unchanged");
      unavailableReported = true;
    }
    return;
  }
  unavailableReported = false;
  double average = 0;
  for (const Input& input : inputs) {
    if (input.fresh) {
      average += input.value / count;
    }
  }
  char value[32];
  const int length = snprintf(value, sizeof(value), "%.6g", average == 0 ? 0 : average);
  if (!std::isfinite(average) || length < 0 ||
      static_cast<size_t>(length) >= sizeof(value)) {
    Serial.println("Feed-Avg ERROR: unable to format average; nothing queued");
    return;
  }
  if (strcmp(value, lastQueuedValue) == 0) {
    retryPending = false;
    return;
  }
  if (retryPending && now - lastRetryAt < kRetryMs) {
    return;
  }
  const aio_publisher::FeedSample sample{"feed-avg", value};
  if (aio_publisher::enqueueGroup("Feed-Avg", &sample, 1)) {
    memcpy(lastQueuedValue, value, static_cast<size_t>(length) + 1);
    retryPending = false;
  } else {
    if (!retryPending) {
      Serial.println("Feed-Avg: average deferred for publish-queue retry");
    }
    retryPending = true;
    lastRetryAt = now;
  }
}

}
