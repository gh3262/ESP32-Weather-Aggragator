#include "weatherkit_current.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "aio_publisher.h"
#include "feed_config.h"
#include "weather_transforms.h"

namespace weatherkit_current {
namespace {
constexpr size_t kMaxPayloadBytes = 4096;
constexpr size_t kMaxPendingSnapshots = 9;
constexpr uint32_t kRetryMs = 2000;

struct Sample {
  const char* key;
  std::string value;
};

using feed_config::Conversion;
using feed_config::Mapping;

struct Snapshot {
  Group group;
  std::string payload;
  uint32_t receivedAt = 0;
  std::vector<Sample> samples;

  Snapshot(Group group, std::string payload, uint32_t receivedAt = 0)
      : group(group), payload(std::move(payload)), receivedAt(receivedAt) {}
};

struct Source {
  Group group;
  const Mapping* mappings;
  size_t count;
  bool received = false;
  uint32_t lastReceivedAt = 0;
  std::map<std::string, std::string> acceptedValues;

  Source(Group group, const Mapping* mappings, size_t count)
      : group(group), mappings(mappings), count(count) {}
};

Source sources[] = {
    {Group::Current, feed_config::weatherkitCurrent,
     sizeof(feed_config::weatherkitCurrent) / sizeof(Mapping)},
    {Group::Minutes5, feed_config::weatherkitMinutes5,
     sizeof(feed_config::weatherkitMinutes5) / sizeof(Mapping)},
    {Group::Minutes30, feed_config::weatherkitMinutes30,
     sizeof(feed_config::weatherkitMinutes30) / sizeof(Mapping)},
    {Group::Hour1, feed_config::weatherkitHour1,
     sizeof(feed_config::weatherkitHour1) / sizeof(Mapping)},
    {Group::Hour2, feed_config::weatherkitHour2,
     sizeof(feed_config::weatherkitHour2) / sizeof(Mapping)},
    {Group::Hour6, feed_config::weatherkitHour6,
     sizeof(feed_config::weatherkitHour6) / sizeof(Mapping)},
    {Group::Hour12, feed_config::weatherkitHour12,
     sizeof(feed_config::weatherkitHour12) / sizeof(Mapping)},
    {Group::Hour24, feed_config::weatherkitHour24,
     sizeof(feed_config::weatherkitHour24) / sizeof(Mapping)},
    {Group::Day1, feed_config::weatherkitDay1,
     sizeof(feed_config::weatherkitDay1) / sizeof(Mapping)},
};
bool healthStarted = false;
bool everConnected = false;
uint32_t healthStartedAt = 0;
uint32_t monitoringStartedAt = 0;

const char* groupName(Group group) {
  switch (group) {
    case Group::Current: return "WeatherKit current";
    case Group::Minutes5: return "WeatherKit m5";
    case Group::Minutes30: return "WeatherKit m30";
    case Group::Hour1: return "WeatherKit hour1";
    case Group::Hour2: return "WeatherKit hour2";
    case Group::Hour6: return "WeatherKit hour6";
    case Group::Hour12: return "WeatherKit hour12";
    case Group::Hour24: return "WeatherKit hour24";
    case Group::Day1: return "WeatherKit day1";
    default: return "WeatherKit unknown";
  }
}

std::mutex receiveLock;
std::deque<Snapshot> incoming;
std::deque<Snapshot> changes;
std::string assembly;
Group assemblyGroup = Group::None;
Group waitingGroup = Group::None;
size_t expectedTotal = 0;
bool assembling = false;
uint32_t receiveFailures = 0;
uint32_t discardedTotal = 0;
uint32_t rawOverflow = 0;
uint32_t assemblyFailures = 0;
uint32_t invalidPayloads = 0;
uint32_t changeOverflow = 0;
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
  if (mapping.conversion == Conversion::Boolean ||
      mapping.conversion == Conversion::ForecastDaylight) {
    if (!input.is<bool>()) {
      return false;
    }
    if (mapping.conversion == Conversion::ForecastDaylight) {
      output = input.as<bool>() ? "It will be light" : "It will be dark";
    } else {
      output = input.as<bool>() ? "Its light" : "Its dark";
    }
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
           const Mapping* mappings, size_t mappingCount,
           Group group = Group::Current) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, payload, DeserializationOption::NestingLimit(8));
  if (error || !document.is<JsonObject>()) {
    Serial.printf("%s ERROR: invalid JSON/object (%s)\n", groupName(group),
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
    const char* separator = strchr(mapping.field, '.');
    const JsonVariantConst input = separator
        ? current[std::string(mapping.field, separator - mapping.field)][separator + 1]
        : current[mapping.field];
    std::string value;
    if (!convert(input, mapping, value)) {
      Serial.printf("%s: skipped missing/invalid field %s\n",
                    groupName(group), mapping.field);
      continue;
    }
    samples.push_back({mapping.feed, value});
  }
  if (!haveEnabledMapping) {
    Serial.printf("%s: no output feeds enabled\n", groupName(group));
    return true;
  }
  if (samples.empty()) {
    Serial.printf("%s ERROR: no valid samples in message\n", groupName(group));
    return false;
  }
  Serial.printf("%s: parsed %u valid feed samples\n", groupName(group),
                static_cast<unsigned>(samples.size()));
  return true;
}
}

void receive(Group group, const char* data, size_t length,
             size_t offset, size_t total) {
  std::lock_guard<std::mutex> guard(receiveLock);
  if (offset == 0) {
    if (assembling) {
      ++receiveFailures;
      ++assemblyFailures;
    }
    assembling = false;
    assembly.clear();
    if (group == Group::None) {
      return;
    }
    bool knownGroup = false;
    for (const Source& source : sources) {
      knownGroup = knownGroup || source.group == group;
    }
    if (!knownGroup) {
      ++receiveFailures;
      ++assemblyFailures;
      return;
    }
    if (total == 0 || total > kMaxPayloadBytes) {
      ++receiveFailures;
      ++assemblyFailures;
      return;
    }
    expectedTotal = total;
    assemblyGroup = group;
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
    ++assemblyFailures;
    return;
  }
  assembly.append(data, length);
  if (assembly.size() == expectedTotal) {
    if (incoming.size() >= kMaxPendingSnapshots) {
      ++receiveFailures;
      ++rawOverflow;
    } else {
      incoming.push_back({assemblyGroup, std::move(assembly), millis()});
    }
    assembly.clear();
    assembling = false;
  }
}

void disconnected() {
  std::lock_guard<std::mutex> guard(receiveLock);
  if (assembling) {
    ++receiveFailures;
    ++assemblyFailures;
  }
  assembling = false;
  assembly.clear();
}

void update(uint32_t now) {
  std::deque<Snapshot> received;
  uint32_t failures;
  uint32_t rawDrops;
  uint32_t chunkDrops;
  {
    std::lock_guard<std::mutex> guard(receiveLock);
    failures = receiveFailures;
    receiveFailures = 0;
    discardedTotal += failures;
    rawDrops = rawOverflow;
    chunkDrops = assemblyFailures;
    received.swap(incoming);
  }
  if (failures) {
    Serial.printf("WeatherKit ERROR: %lu messages discarded "
                  "(cumulative assembly=%lu raw_queue=%lu)\n",
                  static_cast<unsigned long>(failures),
                  static_cast<unsigned long>(chunkDrops),
                  static_cast<unsigned long>(rawDrops));
  }
  for (Snapshot& snapshot : received) {
    for (Source& source : sources) {
      if (source.group != snapshot.group) {
        continue;
      }
      if (!parse(snapshot.payload, snapshot.samples, source.mappings,
                 source.count, snapshot.group)) {
        ++invalidPayloads;
        ++discardedTotal;
        break;
      }
      if (!snapshot.samples.empty()) {
        source.received = true;
        source.lastReceivedAt = snapshot.receivedAt;
      }
      std::vector<Sample> changed;
      for (Sample& sample : snapshot.samples) {
        const auto previous = source.acceptedValues.find(sample.key);
        if (previous == source.acceptedValues.end() || previous->second != sample.value) {
          changed.push_back(std::move(sample));
        }
      }
      if (changed.empty()) {
        Serial.printf("%s unchanged; no receive backlog entry\n", groupName(source.group));
        break;
      }
      if (changes.size() >= kMaxPendingSnapshots) {
        ++changeOverflow;
        ++discardedTotal;
        Serial.printf("%s ERROR: changed-snapshot backlog full; discarded new "
                      "snapshot (cumulative change_queue=%lu)\n",
                      groupName(source.group), static_cast<unsigned long>(changeOverflow));
        break;
      }
      // Commit only admitted values, so a rejected transition can be retried
      // by a later source push. Pending A/B/A transitions remain distinct.
      for (const Sample& sample : changed) {
        source.acceptedValues[sample.key] = sample.value;
      }
      snapshot.payload.clear();
      snapshot.samples = std::move(changed);
      changes.push_back(std::move(snapshot));
      break;
    }
  }
  if (waitingSamples.empty() && !changes.empty()) {
    Snapshot snapshot = std::move(changes.front());
    changes.pop_front();
    waitingGroup = snapshot.group;
    waitingSamples = std::move(snapshot.samples);
  }
  if (waitingSamples.empty() || (retryPending && now - lastRetryAt < kRetryMs)) {
    return;
  }
  std::vector<aio_publisher::FeedSample> samples;
  for (const Sample& sample : waitingSamples) {
    samples.push_back({sample.key, sample.value.c_str()});
  }
  if (aio_publisher::enqueueGroup(groupName(waitingGroup), samples.data(), samples.size())) {
    waitingSamples.clear();
    retryPending = false;
  } else {
    Serial.printf("%s: retaining snapshot for publish-queue retry\n",
                  groupName(waitingGroup));
    retryPending = true;
    lastRetryAt = now;
  }
}

HealthStatus health(bool mqttConnected, uint32_t now) {
  if (!healthStarted) {
    healthStarted = true;
    healthStartedAt = now;
  }
  if (mqttConnected && !everConnected) {
    everConnected = true;
    monitoringStartedAt = now;
  }
  bool enabled = false;
  HealthStatus result = HealthStatus::Green;
  for (const Source& source : sources) {
    if (!sourceStatus(source.group, now).enabled) {
      continue;
    }
    enabled = true;
    const uint32_t age = now - (source.received ? source.lastReceivedAt
                                               : monitoringStartedAt);
    if (everConnected && age >= 300000) {
      result = HealthStatus::Red;
    } else if (result != HealthStatus::Red &&
               (!source.received || age >= 180000)) {
      result = HealthStatus::Amber;
    }
  }
  if (!enabled) {
    return HealthStatus::Off;
  }
  if (!mqttConnected) {
    return everConnected || now - healthStartedAt >= 30000
               ? HealthStatus::Red : HealthStatus::Amber;
  }
  return result;
}

SourceStatus sourceStatus(Group group, uint32_t now) {
  for (const Source& source : sources) {
    if (source.group != group) {
      continue;
    }
    bool enabled = false;
    for (size_t i = 0; i < source.count; ++i) {
      enabled = enabled || source.mappings[i].enabled;
    }
    return {enabled, source.received,
            source.received ? (now - source.lastReceivedAt) / 1000 : 0};
  }
  return {false, false, 0};
}

uint32_t discardedMessages() {
  std::lock_guard<std::mutex> guard(receiveLock);
  return discardedTotal + receiveFailures;
}

DiscardStatus discardStatus() {
  std::lock_guard<std::mutex> guard(receiveLock);
  return {assemblyFailures, rawOverflow, invalidPayloads, changeOverflow};
}
}
