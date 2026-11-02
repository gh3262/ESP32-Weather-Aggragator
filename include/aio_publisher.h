#ifndef AIO_PUBLISHER_H
#define AIO_PUBLISHER_H

#include <stddef.h>
#include <stdint.h>
#include "health_status.h"

namespace aio_publisher {

struct FeedSample {
  const char* key;
  // Final normalized/formatted value; compared byte-for-byte.
  const char* value;
};

void begin();
void update(bool wifiConnected, bool timeSynchronized, uint32_t now);
// Main-loop only.
HealthStatus health(uint32_t now);
HealthStatus weatherHealth(uint32_t now);
// Main-loop only; true after weather input, queued writes, and logs have drained.
bool idle();
// Main-loop only. Copies accepted samples; true also means an unchanged no-op.
// False rejects the entire group without changing queue/cache state.
bool enqueueGroup(const char* groupName, const FeedSample* samples,
                  size_t sampleCount);

// Main-loop only. Timestamped HTTP writes share the MQTT queue and pacing.
// No value-only deduplication: separate dates must retain equal values.
// Called on the main loop only after AIO confirms storage with the requested
// value and UTC timestamp. The caller owns duplicate suppression/persistence.
using HistoryAcknowledged = void (*)();
bool enqueueHistorical(const char* key, const char* value,
                       const char* createdAt, HistoryAcknowledged acknowledged);

}

#endif
