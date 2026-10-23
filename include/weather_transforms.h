#pragma once

#include <cmath>
#include <limits>

namespace weather {

enum class TransformError {
  None,
  NotFinite,
  OutOfRange,
  InvalidPrecision,
  Overflow
};

template <typename T>
struct TransformResult {
  T value;
  TransformError error;

  bool ok() const { return error == TransformError::None; }
};

using NumericResult = TransformResult<double>;
using CompassResult = TransformResult<const char*>;

namespace detail {
inline NumericResult failure(TransformError error) {
  return {std::numeric_limits<double>::quiet_NaN(), error};
}

inline NumericResult checked(double value) {
  return std::isfinite(value) ? NumericResult{value, TransformError::None}
                              : failure(TransformError::Overflow);
}

inline NumericResult scaleNonnegative(double value, double factor) {
  if (!std::isfinite(value)) {
    return failure(TransformError::NotFinite);
  }
  if (value < 0.0) {
    return failure(TransformError::OutOfRange);
  }
  return checked(value * factor);
}
}  // namespace detail

inline NumericResult celsiusToFahrenheit(double celsius) {
  if (!std::isfinite(celsius)) {
    return detail::failure(TransformError::NotFinite);
  }
  if (celsius < -273.15) {
    return detail::failure(TransformError::OutOfRange);
  }
  return detail::checked(celsius * 1.8 + 32.0);
}

inline NumericResult metersPerSecondToMph(double speed) {
  return detail::scaleNonnegative(speed, 3600.0 / 1609.344);
}

inline NumericResult kilometersPerHourToMph(double speed) {
  return detail::scaleNonnegative(speed, 1.0 / 1.609344);
}

inline NumericResult metersToMiles(double distance) {
  return detail::scaleNonnegative(distance, 1.0 / 1609.344);
}

inline NumericResult millimetersToInches(double amount) {
  return detail::scaleNonnegative(amount, 1.0 / 25.4);
}

inline NumericResult hectopascalsToInchesOfMercury(double pressure) {
  if (!std::isfinite(pressure)) {
    return detail::failure(TransformError::NotFinite);
  }
  if (pressure <= 0.0) {
    return detail::failure(TransformError::OutOfRange);
  }
  return detail::checked(pressure / 33.8638866667);
}

inline NumericResult fractionToPercent(double fraction) {
  if (!std::isfinite(fraction)) {
    return detail::failure(TransformError::NotFinite);
  }
  if (fraction < 0.0 || fraction > 1.0) {
    return detail::failure(TransformError::OutOfRange);
  }
  return detail::checked(fraction * 100.0);
}

inline NumericResult secondsToHours(double seconds) {
  return detail::scaleNonnegative(seconds, 1.0 / 3600.0);
}

inline CompassResult degreesToCompass(double degrees) {
  if (!std::isfinite(degrees)) {
    return {nullptr, TransformError::NotFinite};
  }
  // Accept a full turn as north, but reject invalid provider bearings.
  if (degrees < 0.0 || degrees > 360.0) {
    return {nullptr, TransformError::OutOfRange};
  }
  static const char* const points[] = {
      "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
      "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  const unsigned index = static_cast<unsigned>((degrees + 11.25) / 22.5) % 16;
  return {points[index], TransformError::None};
}

inline NumericResult roundToDecimals(double value, unsigned decimals) {
  if (!std::isfinite(value)) {
    return detail::failure(TransformError::NotFinite);
  }
  if (decimals > 6) {
    return detail::failure(TransformError::InvalidPrecision);
  }
  static const double factors[] = {1.0, 10.0, 100.0, 1000.0,
                                   10000.0, 100000.0, 1000000.0};
  const double scaled = value * factors[decimals];
  if (!std::isfinite(scaled)) {
    return detail::failure(TransformError::Overflow);
  }
  const double rounded = std::round(scaled) / factors[decimals];
  return {rounded == 0.0 ? 0.0 : rounded, TransformError::None};
}

}  // namespace weather
