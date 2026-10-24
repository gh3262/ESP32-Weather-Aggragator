#ifndef AIO_PUBLISHER_H
#define AIO_PUBLISHER_H

#include <stddef.h>
#include <stdint.h>

namespace aio_publisher {

struct FeedSample {
  const char* key;
  // Final normalized/formatted value; compared byte-for-byte.
  const char* value;
};

void begin();
void update(bool wifiConnected, bool timeSynchronized, uint32_t now);
// Main-loop only. Copies accepted samples; true also means an unchanged no-op.
// False rejects the entire group without changing queue/cache state.
bool enqueueGroup(const char* groupName, const FeedSample* samples,
                  size_t sampleCount);

}

#endif
