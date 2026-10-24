#ifndef DIAGNOSTIC_SUMMARY_H
#define DIAGNOSTIC_SUMMARY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace diagnostic_summary {
constexpr size_t kMaxBytes = 1024;

struct Topic {
  const char* name;
  uint32_t messages;
  uint32_t changes;
  uint32_t retained;
  uint32_t maxBytes;
  uint32_t averageSeconds;
  uint32_t ageSeconds;
};

struct Status {
  const char* stamp;
  bool connected;
  uint32_t connects;
  uint32_t disconnects;
  uint32_t errors;
  int32_t rssi;
  uint32_t freeHeap;
  uint32_t minHeap;
};

inline bool format(char (&out)[kMaxBytes + 1], const Status& status,
                   const Topic* topics, size_t count) {
  int written = snprintf(
      out, sizeof(out),
      "WeatherKit %s\nMQTT=%s c/d/e=%lu/%lu/%lu RSSI=%ld heap=%lu min=%lu\n"
      "topic: msgs,changed,retained,maxB,avgS,ageS (-=no data)\n",
      status.stamp, status.connected ? "up" : "down",
      static_cast<unsigned long>(status.connects),
      static_cast<unsigned long>(status.disconnects),
      static_cast<unsigned long>(status.errors), static_cast<long>(status.rssi),
      static_cast<unsigned long>(status.freeHeap),
      static_cast<unsigned long>(status.minHeap));
  if (written < 0 || static_cast<size_t>(written) >= sizeof(out)) {
    return false;
  }
  size_t used = static_cast<size_t>(written);
  for (size_t i = 0; i < count; ++i) {
    const Topic& topic = topics[i];
    char average[16] = "-";
    char age[16] = "-";
    if (topic.messages > 1) {
      snprintf(average, sizeof(average), "%lu",
               static_cast<unsigned long>(topic.averageSeconds));
    }
    if (topic.messages > 0) {
      snprintf(age, sizeof(age), "%lu",
               static_cast<unsigned long>(topic.ageSeconds));
    }
    written = snprintf(out + used, sizeof(out) - used, "%s: %lu,%lu,%lu,%lu,%s,%s\n",
                       topic.name, static_cast<unsigned long>(topic.messages),
                       static_cast<unsigned long>(topic.changes),
                       static_cast<unsigned long>(topic.retained),
                       static_cast<unsigned long>(topic.maxBytes), average, age);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(out) - used) {
      return false;
    }
    used += static_cast<size_t>(written);
  }
  return true;
}
}

#endif
