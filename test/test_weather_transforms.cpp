#include "weather_transforms.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
unsigned checks = 0;
unsigned failures = 0;

void check(bool condition, const char* description) {
  ++checks;
  if (!condition) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
  }
}

void expectValue(weather::NumericResult result, double expected,
                 const char* description, double tolerance = 1e-9) {
  check(result.ok() && std::fabs(result.value - expected) <= tolerance,
        description);
}

void expectError(weather::NumericResult result, weather::TransformError error,
                 const char* description) {
  check(!result.ok() && result.error == error && std::isnan(result.value),
        description);
}
}  // namespace

int main() {
  using namespace weather;
  expectValue(celsiusToFahrenheit(0.0), 32.0, "freezing");
  expectValue(celsiusToFahrenheit(100.0), 212.0, "boiling");
  expectValue(celsiusToFahrenheit(-40.0), -40.0, "negative temperature");
  expectValue(celsiusToFahrenheit(-273.15), -459.67, "absolute zero");
  expectError(celsiusToFahrenheit(-273.16), TransformError::OutOfRange,
              "below absolute zero");
  expectValue(metersPerSecondToMph(1.0), 2.2369362920544, "m/s to mph");
  expectValue(kilometersPerHourToMph(100.0), 62.1371192237334, "km/h to mph");
  expectValue(metersToMiles(1609.344), 1.0, "meters to miles");
  expectValue(millimetersToInches(25.4), 1.0, "mm to inches");
  expectValue(hectopascalsToInchesOfMercury(1013.25), 29.9213,
              "standard atmosphere in inHg", 0.00005);
  expectValue(hectopascalsToInchesOfMercury(33.8638866667), 1.0,
              "hPa to one conventional inHg");
  expectValue(secondsToHours(5400.0), 1.5, "seconds to hours");
  expectValue(fractionToPercent(0.425), 42.5, "fraction to percent");
  expectValue(fractionToPercent(0.0), 0.0, "zero fraction");
  expectValue(fractionToPercent(1.0), 100.0, "full fraction");
  expectError(fractionToPercent(42.5), TransformError::OutOfRange,
              "do not convert an already-percent value");
  expectError(fractionToPercent(-0.1), TransformError::OutOfRange,
              "negative fraction");
  expectError(hectopascalsToInchesOfMercury(0.0), TransformError::OutOfRange,
              "zero absolute pressure");
  expectError(hectopascalsToInchesOfMercury(-1.0), TransformError::OutOfRange,
              "negative absolute pressure");

  using Conversion = NumericResult (*)(double);
  const Conversion nonnegativeConversions[] = {
      metersPerSecondToMph, kilometersPerHourToMph, metersToMiles,
      millimetersToInches, secondsToHours};
  for (Conversion convert : nonnegativeConversions) {
    expectValue(convert(0.0), 0.0, "zero measurement remains valid");
    expectError(convert(-1.0), TransformError::OutOfRange,
                "negative measurement rejected");
  }

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  const Conversion conversions[] = {
      celsiusToFahrenheit, metersPerSecondToMph, kilometersPerHourToMph,
      metersToMiles, millimetersToInches, hectopascalsToInchesOfMercury,
      fractionToPercent, secondsToHours};
  for (Conversion convert : conversions) {
    expectError(convert(nan), TransformError::NotFinite, "NaN rejected");
    expectError(convert(infinity), TransformError::NotFinite,
                "positive infinity rejected");
    expectError(convert(-infinity), TransformError::NotFinite,
                "negative infinity rejected");
  }
  const double maximum = std::numeric_limits<double>::max();
  expectError(celsiusToFahrenheit(maximum), TransformError::Overflow,
              "temperature overflow");
  expectError(metersPerSecondToMph(maximum), TransformError::Overflow,
              "wind overflow");

  const char* points[] = {
      "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
      "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  for (unsigned i = 0; i < 16; ++i) {
    const CompassResult center = degreesToCompass(i * 22.5);
    check(center.ok() && std::strcmp(center.value, points[i]) == 0,
          "all compass sector centers");
    const double boundary = i * 22.5 + 11.25;
    const CompassResult below = degreesToCompass(boundary - 0.000001);
    const CompassResult at = degreesToCompass(boundary);
    check(below.ok() && std::strcmp(below.value, points[i]) == 0,
          "below compass boundary");
    check(at.ok() && std::strcmp(at.value, points[(i + 1) % 16]) == 0,
          "compass boundary rounds clockwise");
  }
  const CompassResult fullTurn = degreesToCompass(360.0);
  check(fullTurn.ok() && std::strcmp(fullTurn.value, "N") == 0, "360 is north");
  for (double invalid : {nan, infinity, -infinity, -1.0, 360.01}) {
    const CompassResult result = degreesToCompass(invalid);
    check(!result.ok() && result.value == nullptr &&
              result.error == (std::isfinite(invalid) ? TransformError::OutOfRange
                                                     : TransformError::NotFinite),
          "invalid compass input rejected");
  }

  expectValue(roundToDecimals(72.46, 1), 72.5, "one decimal");
  expectValue(roundToDecimals(72.44, 1), 72.4, "round down");
  expectValue(roundToDecimals(2.5, 0), 3.0, "positive tie away from zero");
  expectValue(roundToDecimals(-2.5, 0), -3.0, "negative tie away from zero");
  expectValue(roundToDecimals(1.1234564, 6), 1.123456, "six decimals");
  const NumericResult zero = roundToDecimals(-0.01, 1);
  check(zero.ok() && zero.value == 0.0 && !std::signbit(zero.value),
        "negative zero normalized");
  expectError(roundToDecimals(1.0, 7), TransformError::InvalidPrecision,
              "unsupported precision");
  expectError(roundToDecimals(nan, 1), TransformError::NotFinite,
              "NaN rounding rejected");
  expectError(roundToDecimals(infinity, 1), TransformError::NotFinite,
              "infinite rounding rejected");
  expectError(roundToDecimals(maximum, 1), TransformError::Overflow,
              "rounding overflow");
  const NumericResult temperature = celsiusToFahrenheit(22.48);
  check(temperature.ok(), "conversion before rounding");
  if (temperature.ok()) {
    expectValue(roundToDecimals(temperature.value, 1), 72.5,
                "Fahrenheit publication precision");
  }
  std::printf("%u checks, %u failures\n", checks, failures);
  return failures ? 1 : 0;
}
