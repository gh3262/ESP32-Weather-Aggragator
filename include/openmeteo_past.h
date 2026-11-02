#pragma once

#include <stdint.h>

namespace openmeteo_past {

void begin();
void update(bool wifiConnected, bool timeSynchronized, uint32_t now);

}
