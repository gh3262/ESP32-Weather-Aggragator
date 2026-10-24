#ifndef AIO_PUBLISHER_H
#define AIO_PUBLISHER_H

#include <stddef.h>

namespace aio_publisher {

struct FeedSample {
  const char* key;
  const char* value;
};

void begin();
void update(bool wifiConnected, bool timeSynchronized, unsigned long now);
bool enqueueGroup(const char* groupName, const FeedSample* samples,
                  size_t sampleCount);

}

#endif
