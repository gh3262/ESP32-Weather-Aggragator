#ifndef OPENMETEO_CURRENT_H
#define OPENMETEO_CURRENT_H

#include <stdint.h>
#include "health_status.h"

namespace openmeteo_current {

void begin();
void update(bool wifiConnected, bool timeSynchronized, uint32_t now);
HealthStatus health(uint32_t now);

}

#endif
