#pragma once

#include <stdint.h>

namespace status_pixels {
constexpr uint16_t kCount = 8;

bool begin();
bool setPixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue);
void clear();
}  // namespace status_pixels
