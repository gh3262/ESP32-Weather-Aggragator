#ifndef NWS_ALERTS_H
#define NWS_ALERTS_H

#include <stdint.h>
#include "health_status.h"

namespace nws_alerts {

void begin();
void update(bool wifiConnected, bool timeSynchronized, uint32_t now);
HealthStatus health(uint32_t now);

}

#endif
