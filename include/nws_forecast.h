#ifndef NWS_FORECAST_H
#define NWS_FORECAST_H

#include <stdint.h>
#include "health_status.h"

namespace nws_forecast {
void begin();
void update(bool wifiConnected, bool timeSynchronized, uint32_t now);
HealthStatus health(uint32_t now);
}

#endif
