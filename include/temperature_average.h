#ifndef TEMPERATURE_AVERAGE_H
#define TEMPERATURE_AVERAGE_H

#include <stdint.h>

namespace temperature_average {

void begin();
// Main-loop only. Record successfully parsed Fahrenheit samples, including
// unchanged values. Unrelated feed keys are ignored.
void observe(const char* key, const char* value, uint32_t receivedAt);
void update(uint32_t now);

}

#endif
