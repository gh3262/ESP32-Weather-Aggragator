#include "status_pixels.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

namespace status_pixels {
namespace {
constexpr uint8_t kBrightness = 16;
Adafruit_NeoPixel ring(kCount, D0, NEO_GRB + NEO_KHZ800);
bool ready = false;
}  // namespace

bool begin() {
  if (ready) {
    return true;
  }
  if (ring.numPixels() != kCount || !ring.begin()) {
    Serial.println("ERROR: unable to initialize NeoPixel ring on D0");
    return false;
  }
  ring.setBrightness(kBrightness);
  ready = true;
  clear();
  return true;
}

bool setPixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue) {
  if (!ready) {
    Serial.println("ERROR: NeoPixel write attempted before initialization");
    return false;
  }
  if (index >= kCount) {
    Serial.printf("ERROR: NeoPixel index %u outside ring size %u\n",
                  static_cast<unsigned>(index), static_cast<unsigned>(kCount));
    return false;
  }
  const uint32_t color = ring.Color(red, green, blue);
  ring.setPixelColor(index, color);
  ring.show();
  return true;
}

void clear() {
  if (!ready) {
    Serial.println("ERROR: NeoPixel clear attempted before initialization");
    return;
  }
  ring.clear();
  ring.show();
}
}  // namespace status_pixels
