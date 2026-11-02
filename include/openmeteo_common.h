#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "weather_transforms.h"

namespace openmeteo_common {

inline bool validCoordinate(const char* text, double minimum, double maximum) {
  if (!text || !text[0]) return false;
  char* end = nullptr;
  const double value = strtod(text, &end);
  return end != text && *end == '\0' && std::isfinite(value) &&
         value >= minimum && value <= maximum;
}

inline bool numeric(JsonVariantConst input, double& value) {
  if (input.is<bool>() || !input.is<double>()) return false;
  std::string serialized;
  serializeJson(input, serialized);
  char* end = nullptr;
  value = strtod(serialized.c_str(), &end);
  return end != serialized.c_str() && *end == '\0' && std::isfinite(value);
}

inline std::string rawNumber(double value) {
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%.6g", value == 0 ? 0.0 : value);
  return buffer;
}

inline bool unitIs(JsonVariantConst input, const char* expected) {
  return input.is<const char*>() &&
         strcmp(input.as<const char*>(), expected) == 0;
}

inline const char* conditionText(unsigned code) {
  struct ConditionText { unsigned code; const char* text; };
  static const ConditionText conditions[] = {
      {0, "Clear sky"}, {1, "Mainly clear"}, {2, "Partly cloudy"},
      {3, "Overcast"}, {45, "Fog"}, {48, "Depositing rime fog"},
      {51, "Light drizzle"}, {53, "Moderate drizzle"}, {55, "Dense drizzle"},
      {56, "Light freezing drizzle"}, {57, "Dense freezing drizzle"},
      {61, "Slight rain"}, {63, "Rain"}, {65, "Heavy rain"},
      {66, "Light freezing rain"}, {67, "Heavy freezing rain"},
      {71, "Slight snowfall"}, {73, "Moderate snowfall"}, {75, "Heavy snowfall"},
      {77, "Snow grains"}, {80, "Slight rain showers"},
      {81, "Moderate rain showers"}, {82, "Violent rain showers"},
      {85, "Slight snow showers"}, {86, "Heavy snow showers"},
      {95, "Thunderstorms - slight or moderate"},
      {96, "Thunderstorms with slight hail"}, {99, "Thunderstorms with heavy hail"}};
  for (const ConditionText& entry : conditions) {
    if (entry.code == code) return entry.text;
  }
  return nullptr;
}

inline const char* windText(double degrees) {
  const char* point = weather::degreesToCompass(degrees).value;
  if (!strcmp(point, "N")) return "North";
  if (!strcmp(point, "E")) return "East";
  if (!strcmp(point, "S")) return "South";
  if (!strcmp(point, "W")) return "West";
  return point;
}

}
