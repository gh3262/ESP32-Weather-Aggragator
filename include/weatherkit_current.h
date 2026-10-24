#ifndef WEATHERKIT_CURRENT_H
#define WEATHERKIT_CURRENT_H

#include <stddef.h>
#include <stdint.h>

namespace weatherkit_current {

// MQTT-task entry points; parsing and publication stay on the main loop.
void receive(bool currentTopic, const char* data, size_t length,
             size_t offset, size_t total);
void disconnected();
void update(uint32_t now);

}

#endif
