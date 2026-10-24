#ifndef WEATHERKIT_CURRENT_H
#define WEATHERKIT_CURRENT_H

#include <stddef.h>
#include <stdint.h>
#include "health_status.h"

namespace weatherkit_current {

enum class Group { None, Current, Minutes5, Minutes30, Hour1 };

// MQTT-task entry points; parsing and publication stay on the main loop.
void receive(Group group, const char* data, size_t length,
             size_t offset, size_t total);
void disconnected();
void update(uint32_t now);
// Main-loop only. Disabled source groups do not participate in freshness.
HealthStatus health(bool mqttConnected, uint32_t now);

}

#endif
