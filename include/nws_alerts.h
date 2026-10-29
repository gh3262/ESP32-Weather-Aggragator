#ifndef NWS_ALERTS_H
#define NWS_ALERTS_H

#include <stdint.h>
#include "health_status.h"

namespace nws_alerts {

enum class AlertLevel { None, Statement, Watch, Warning };

struct AlertPixel {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

void begin();
void update(bool wifiConnected, bool timeSynchronized, uint32_t now);
HealthStatus health(uint32_t now);
AlertPixel alertPixel(uint32_t now);

}

#endif
