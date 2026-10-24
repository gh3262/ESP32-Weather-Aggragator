#include "weatherkit_current.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "aio_publisher.h"
#include "feed_config.h"
#include "weather_transforms.h"

namespace weatherkit_current {
namespace {
constexpr size_t kMaxPayloadBytes = 4096;
constexpr size_t kMaxPendingSnapshots = 4;
constexpr uint32_t kRetryMs = 2000;

struct Sample {
  const char* key;
  std::string value;
};

using feed_config::Conversion;
using feed_config::Mapping;

std::mutex receiveLock;
std::deque<std::string> incoming;
std::string assembly;
size_t expectedTotal = 0;
bool assembling = false;
uint32_t receiveFailures = 0;
uint32_t lastRetryAt = 0;
bool retryPending = false;
std::vector<Sample> waitingSamples;

bool convert(JsonVariantConst input, const Mapping& mapping, std::string& output) {
  if (mapping.conversion == Conversion::Text) {
    if (!input.is<const char*>()) {
      return false;
    }
    const char* text = input.as<const char*>();
    const size_t length = strlen(text);
    if (length == 0 || length > 128) {
      return false;
    }
    output = text;
    return true;
  }
  if (mapping.conversion == Conversion::Boolean) {
    if (!input.is<bool>()) {
      return false;
    }
    output = input.as<bool>() ? "Its light" : "Its dark";
    return true;
  }
  if (input.is<bool>() || !input.is<double>() ||
      !std::isfinite(input.as<double>())) {
    return false;
  }
  // ArduinoJson may store short decimals as float32. Re-read its canonical
  // decimal text in double precision before unit conversion and truncation.
  std::string numeric;
  serializeJson(input, numeric);
  const double value = std::strtod(numeric.c_str(), nullptr);
  if (!std::isfinite(value)) {
    return false;
  }
  weather::NumericResult result{value, weather::TransformError::None};
  switch (mapping.conversion) {
    case Conversion::Percent:
      result = weather::fractionToPercent(value);
      break;
    case Conversion::Temperature:
      result = weather::celsiusToFahrenheit(value);
      if (result.ok()) {
        result = weather::roundToDecimals(result.value, 1);
      }
      break;
    case Conversion::Miles:
      result = weather::metersToMiles(value);
      break;
    case Conversion::Mph:
      result = weather::kilometersPerHourToMph(value);
      break;
    case Conversion::Bearing:
      if (!weather::degreesToCompass(value).ok()) {
        return false;
      }
      break;
    case Conversion::Compass: {
      const auto compass = weather::degreesToCompass(value);
      if (!compass.ok()) {
        return false;
      }
      output = compass.value;
      return true;
    }
    case Conversion::Raw:
      if (value < 0 || (strcmp(mapping.field, "pressure") == 0 && value == 0)) {
        return false;
      }
      break;
    default:
      return false;
  }
  if (!result.ok()) {
    return false;
  }
  const bool wholeUnits = mapping.conversion == Conversion::Percent ||
                          mapping.conversion == Conversion::Miles ||
                          mapping.conversion == Conversion::Mph;
  char formatted[64];
  int length;
  if (mapping.conversion == Conversion::Temperature) {
    length = snprintf(formatted, sizeof(formatted), "%.1f", result.value);
  } else if (wholeUnits) {
    length = snprintf(formatted, sizeof(formatted), "%.0f", std::trunc(result.value));
  } else {
    output = numeric;
    return true;
  }
  if (length < 0 || static_cast<size_t>(length) >= sizeof(formatted)) {
    return false;
  }
  output = formatted;
  return true;
}

bool parse(const std::string& payload, std::vector<Sample>& samples,
           const Mapping* mappings, size_t mappingCount) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, payload, DeserializationOption::NestingLimit(8));
  if (error || !document.is<JsonObject>()) {
    Serial.printf("WeatherKit current ERROR: invalid JSON/object (%s)\n",
                  error ? error.c_str() : "root is not an object");
    return false;
  }
  const JsonObjectConst current = document.as<JsonObjectConst>();
  bool haveEnabledMapping = false;
  for (size_t i = 0; i < mappingCount; ++i) {
    const Mapping& mapping = mappings[i];
    if (!mapping.enabled) {
      continue;
    }
    haveEnabledMapping = true;
    const JsonVariantConst input = current[mapping.field];
    std::string value;
    if (!convert(input, mapping, value)) {
      Serial.printf("WeatherKit current: skipped missing/invalid field %s\n",
                    mapping.field);
      continue;
    }
    samples.push_back({mapping.feed, value});
  }
  if (!haveEnabledMapping) {
    Serial.println("WeatherKit current: no output feeds enabled");
    return true;
  }
  if (samples.empty()) {
    Serial.println("WeatherKit current ERROR: no valid samples in message");
    return false;
  }
  Serial.printf("WeatherKit current: parsed %u valid feed samples\n",
                static_cast<unsigned>(samples.size()));
  return true;
}
}

void receive(bool currentTopic, const char* data, size_t length,
             size_t offset, size_t total) {
  std::lock_guard<std::mutex> guard(receiveLock);
  if (offset == 0) {
    if (assembling) {
      ++receiveFailures;
    }
    assembling = false;
    assembly.clear();
    if (!currentTopic) {
      return;
    }
    if (total == 0 || total > kMaxPayloadBytes) {
      ++receiveFailures;
      return;
    }
    expectedTotal = total;
    assembling = true;
  }
  if (!assembling) {
    return;
  }
  if (!data || offset != assembly.size() || total != expectedTotal ||
      length > expectedTotal - assembly.size()) {
    assembling = false;
    assembly.clear();
    ++receiveFailures;
    return;
  }
  assembly.append(data, length);
  if (assembly.size() == expectedTotal) {
    if (incoming.size() >= kMaxPendingSnapshots) {
      ++receiveFailures;
    } else {
      incoming.push_back(std::move(assembly));
    }
    assembly.clear();
    assembling = false;
  }
}

void disconnected() {
  std::lock_guard<std::mutex> guard(receiveLock);
  if (assembling) {
    ++receiveFailures;
  }
  assembling = false;
  assembly.clear();
}

void update(uint32_t now) {
  std::string payload;
  uint32_t failures;
  {
    std::lock_guard<std::mutex> guard(receiveLock);
    failures = receiveFailures;
    receiveFailures = 0;
    if (waitingSamples.empty() && !incoming.empty()) {
      payload = std::move(incoming.front());
      incoming.pop_front();
    }
  }
  if (failures) {
    Serial.printf("WeatherKit current ERROR: %lu messages discarded "
                  "(oversize, invalid/incomplete chunks, or receive queue full)\n",
                  static_cast<unsigned long>(failures));
  }
  if (!payload.empty()) {
    parse(payload, waitingSamples, feed_config::weatherkitCurrent,
          sizeof(feed_config::weatherkitCurrent) /
              sizeof(feed_config::weatherkitCurrent[0]));
  }
  if (waitingSamples.empty() || (retryPending && now - lastRetryAt < kRetryMs)) {
    return;
  }
  std::vector<aio_publisher::FeedSample> samples;
  for (const Sample& sample : waitingSamples) {
    samples.push_back({sample.key, sample.value.c_str()});
  }
  if (aio_publisher::enqueueGroup("WeatherKit current", samples.data(), samples.size())) {
    waitingSamples.clear();
    retryPending = false;
  } else {
    Serial.println("WeatherKit current: retaining snapshot for publish-queue retry");
    retryPending = true;
    lastRetryAt = now;
  }
}
}
