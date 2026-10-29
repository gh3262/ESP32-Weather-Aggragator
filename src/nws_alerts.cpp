#include "nws_alerts.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "aio_publisher.h"
#include "https_gate.h"
#include "letsencrypt_root_ca.h"

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

#ifndef NWS_ALERTS_LATITUDE
#define NWS_ALERTS_LATITUDE LATITUDE
#endif

#ifndef NWS_ALERTS_LONGITUDE
#define NWS_ALERTS_LONGITUDE LONGITUDE
#endif

namespace nws_alerts {
namespace {
constexpr uint32_t kPollIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kRetryDelayMs = 60UL * 1000UL;
constexpr uint32_t kRotationIntervalMs = 15UL * 1000UL;
constexpr uint32_t kPublishRetryMs = 2000;
constexpr uint32_t kStaleAfterMs = 2UL * kPollIntervalMs;
constexpr uint32_t kQueueDelayAmberMs = 2UL * 60UL * 1000UL;
constexpr size_t kMaxResponseBytes = 16 * 1024;
constexpr size_t kMaxAlerts = 5;
constexpr size_t kMaxFeedValueLength = 512;
constexpr size_t kTruncationSuffixLength = 12;
constexpr char kGroupName[] = "NWS Alerts";
constexpr char kUserAgent[] = "ESP32-Weather-Aggregator/1.0 (NWS alerts)";

struct AlertContent {
  std::string weather;
  std::string headline;
  std::string details;
};

struct Sample {
  const char* key;
  std::string value;
};

struct FetchResult {
  bool overflow = false;
  int status = 0;
  std::unique_ptr<char[]> body;
  size_t bodyLength = 0;
};

std::mutex resultLock;
FetchResult workerResult;
bool workerRunning = false;
bool resultPending = false;
bool configured = false;
bool attempted = false;
uint32_t lastAttemptAt = 0;
uint32_t nextAttemptDelay = 0;
std::vector<AlertContent> alerts;
std::vector<Sample> pendingSamples;
size_t nextAlertIndex = 0;
size_t pendingAlertIndex = 0;
bool pendingIsAlert = false;
bool snapshotPending = false;
uint32_t pendingSince = 0;
uint32_t lastCycleAt = 0;
uint32_t lastAcceptedAt = 0;
bool haveAccepted = false;
bool publishRetryPending = false;
uint32_t lastPublishRetryAt = 0;
uint32_t failedRequests = 0;

class BoundedBody : public Stream {
 public:
  explicit BoundedBody(char* destination) : data(destination) {}

  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t size) override {
    const size_t copied = size < kMaxResponseBytes - length
                              ? size
                              : kMaxResponseBytes - length;
    if (copied) {
      memcpy(data + length, bytes, copied);
      length += copied;
      data[length] = '\0';
    }
    overflow = overflow || copied != size;
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  char* data;
  size_t length = 0;
  bool overflow = false;
};

bool validCoordinate(const char* text, double minimum, double maximum) {
  if (!text || !text[0]) {
    return false;
  }
  char* end = nullptr;
  const double value = strtod(text, &end);
  return end != text && *end == '\0' && std::isfinite(value) &&
         value >= minimum && value <= maximum;
}

bool isValidUtf8Sequence(const char* text, size_t length, size_t index,
                         size_t& sequenceLength) {
  const unsigned char first = static_cast<unsigned char>(text[index]);
  if (first < 0x80) {
    sequenceLength = 1;
    return true;
  }
  if ((first & 0xE0) == 0xC0) {
    sequenceLength = 2;
  } else if ((first & 0xF0) == 0xE0) {
    sequenceLength = 3;
  } else if ((first & 0xF8) == 0xF0) {
    sequenceLength = 4;
  } else {
    sequenceLength = 1;
    return false;
  }
  if (index + sequenceLength > length) {
    return false;
  }
  for (size_t offset = 1; offset < sequenceLength; ++offset) {
    if ((static_cast<unsigned char>(text[index + offset]) & 0xC0) != 0x80) {
      sequenceLength = 1;
      return false;
    }
  }
  return true;
}

class BoundedText {
 public:
  void append(const char* text) {
    if (!text || !text[0]) {
      return;
    }
    const size_t length = strlen(text);
    const size_t prefixLimit =
        kMaxFeedValueLength - kTruncationSuffixLength;
    size_t available = prefixLimit - value.size();
    size_t copied = 0;
    while (copied < length && available) {
      size_t sequenceLength = 1;
      isValidUtf8Sequence(text, length, copied, sequenceLength);
      if (sequenceLength > available) {
        break;
      }
      value.append(text + copied, sequenceLength);
      copied += sequenceLength;
      available -= sequenceLength;
    }
    truncated = truncated || copied != length;
  }

  std::string finish(const char* feed) {
    if (truncated) {
      value += " [truncated]";
      Serial.printf("NWS Alerts: %s truncated to %u bytes\n", feed,
                    static_cast<unsigned>(kMaxFeedValueLength));
    }
    return std::move(value);
  }

 private:
  std::string value;
  bool truncated = false;
};

const char* textValue(JsonVariantConst input) {
  return input.is<const char*>() ? input.as<const char*>() : "";
}

void appendCommonAlert(BoundedText& output, const JsonObjectConst& properties,
                       const char* nwsHeadline, size_t alertIndex,
                       size_t totalAlerts) {
  const std::string header =
      "** ALERT " + std::to_string(alertIndex + 1) + " of " +
      std::to_string(totalAlerts) + " ** ";
  output.append(header.c_str());
  output.append("From ");
  output.append(textValue(properties["senderName"]));
  output.append(" sent ");
  output.append(textValue(properties["sent"]));
  output.append("\n>> ");
  output.append(textValue(properties["severity"]));
  output.append(" * ");
  output.append(textValue(properties["urgency"]));
  output.append(" <<\n");
  output.append(textValue(properties["event"]));
  output.append(": ");
  output.append(nwsHeadline);
  output.append("\n");
  output.append(textValue(properties["headline"]));
  output.append("\n");
}

AlertContent formatAlert(const JsonObjectConst& properties,
                         size_t alertIndex, size_t totalAlerts) {
  const JsonArrayConst nwsHeadlines =
      properties["parameters"]["NWSheadline"].as<JsonArrayConst>();
  const char* nwsHeadline =
      nwsHeadlines.size() ? textValue(nwsHeadlines[0]) : "";

  BoundedText weather;
  appendCommonAlert(weather, properties, nwsHeadline, alertIndex, totalAlerts);

  BoundedText details;
  appendCommonAlert(details, properties, nwsHeadline, alertIndex, totalAlerts);
  details.append("\n");
  details.append(textValue(properties["description"]));
  details.append("\n");

  BoundedText headline;
  headline.append(nwsHeadline);
  headline.append(" - ");
  headline.append(textValue(properties["headline"]));

  AlertContent content;
  content.weather = weather.finish("weather-alerts");
  content.headline = headline.finish("headline-alert");
  content.details = details.finish("big-alert");
  return content;
}

bool parseResponse(char* body, size_t bodyLength,
                   std::vector<AlertContent>& parsedAlerts) {
  if (!body || !bodyLength) {
    Serial.println("NWS Alerts ERROR: empty response body");
    return false;
  }

  JsonDocument filter;
  filter["features"][0]["properties"]["sent"] = true;
  filter["features"][0]["properties"]["severity"] = true;
  filter["features"][0]["properties"]["urgency"] = true;
  filter["features"][0]["properties"]["event"] = true;
  filter["features"][0]["properties"]["senderName"] = true;
  filter["features"][0]["properties"]["headline"] = true;
  filter["features"][0]["properties"]["description"] = true;
  filter["features"][0]["properties"]["parameters"]["NWSheadline"][0] = true;

  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, body, DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(8));
  if (error || !document.is<JsonObject>() ||
      !document["features"].is<JsonArray>()) {
    Serial.printf("NWS Alerts ERROR: invalid JSON/object (%s)\n",
                  error ? error.c_str() : "missing features array");
    return false;
  }

  const JsonArrayConst features =
      document["features"].as<JsonArrayConst>();
  const size_t featureCount =
      features.size() < kMaxAlerts ? features.size() : kMaxAlerts;
  for (size_t index = 0; index < featureCount; ++index) {
    const JsonVariantConst feature = features[index];
    if (!feature["properties"].is<JsonObjectConst>()) {
      Serial.printf("NWS Alerts: skipped malformed feature %u\n",
                    static_cast<unsigned>(index + 1));
      continue;
    }
    const JsonObjectConst properties =
        feature["properties"].as<JsonObjectConst>();
    if (!properties["event"].is<const char*>() ||
        !properties["event"].as<const char*>()[0]) {
      Serial.printf("NWS Alerts: skipped feature %u without an event\n",
                    static_cast<unsigned>(index + 1));
      continue;
    }
    parsedAlerts.push_back(
        formatAlert(properties, parsedAlerts.size(), featureCount));
  }
  if (featureCount && parsedAlerts.empty()) {
    Serial.println("NWS Alerts ERROR: no usable alert features");
    return false;
  }
  Serial.printf("NWS Alerts: parsed %u of %u available alert features\n",
                static_cast<unsigned>(parsedAlerts.size()),
                static_cast<unsigned>(features.size()));
  return true;
}

FetchResult fetchAlerts() {
  FetchResult result;
  WiFiClientSecure transport;
  transport.setCACert(letsencrypt_root_ca::kTrustedRootCa);
  transport.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  const std::string url =
      std::string("https://api.weather.gov/alerts/active?point=") +
      NWS_ALERTS_LATITUDE + "," + NWS_ALERTS_LONGITUDE;
  if (http.begin(transport, url.c_str())) {
    http.addHeader("User-Agent", kUserAgent);
    http.addHeader("Accept", "application/geo+json");
    result.status = http.GET();
    if (result.status == HTTP_CODE_OK) {
      result.body.reset(new (std::nothrow) char[kMaxResponseBytes + 1]);
      if (!result.body) {
        result.overflow = true;
      } else {
        BoundedBody body(result.body.get());
        const int read = http.writeToStream(&body);
        result.overflow = body.overflow || read < 0;
        result.bodyLength = body.length;
      }
    }
    http.end();
  }
  return result;
}

void fetchTask(void*) {
  FetchResult result = fetchAlerts();
  https_gate::release();
  {
    std::lock_guard<std::mutex> guard(resultLock);
    workerResult = std::move(result);
    resultPending = true;
    workerRunning = false;
  }
  vTaskDelete(nullptr);
}

void failRequest(uint32_t now, const char* reason, int status) {
  ++failedRequests;
  nextAttemptDelay = kRetryDelayMs;
  Serial.printf(
      "NWS Alerts ERROR: request failed (%s, HTTP=%d); retry in 60 seconds\n",
      reason, status);
  lastAttemptAt = now;
}

void consumeWorkerResult(uint32_t now) {
  FetchResult result;
  {
    std::lock_guard<std::mutex> guard(resultLock);
    if (!resultPending) {
      return;
    }
    result = std::move(workerResult);
    workerResult = FetchResult{};
    resultPending = false;
  }
  if (result.status != HTTP_CODE_OK || result.overflow ||
      !result.body || !result.bodyLength) {
    failRequest(now,
                result.overflow ? "response too large or buffer unavailable"
                                : "transport/status",
                result.status);
    return;
  }
  std::vector<AlertContent> parsedAlerts;
  if (!parseResponse(result.body.get(), result.bodyLength, parsedAlerts)) {
    failRequest(now, "invalid response", result.status);
    return;
  }
  alerts = std::move(parsedAlerts);
  nextAlertIndex = 0;
  snapshotPending = true;
  failedRequests = 0;
  nextAttemptDelay = kPollIntervalMs;
  Serial.printf("NWS Alerts: HTTPS request succeeded; %u alerts retained\n",
                static_cast<unsigned>(alerts.size()));
}

void prepareSamples(uint32_t now) {
  if (!pendingSamples.empty()) {
    return;
  }
  if (!snapshotPending && alerts.size() < 2) {
    return;
  }
  if (!snapshotPending &&
      now - lastCycleAt < kRotationIntervalMs) {
    return;
  }

  pendingSamples.clear();
  pendingSamples.push_back(
      {"nbr-alerts", std::to_string(alerts.size())});
  pendingIsAlert = !alerts.empty();
  if (pendingIsAlert) {
    pendingAlertIndex = nextAlertIndex % alerts.size();
    const AlertContent& alert = alerts[pendingAlertIndex];
    pendingSamples.push_back({"weather-alerts", alert.weather});
    pendingSamples.push_back({"headline-alert", alert.headline});
    pendingSamples.push_back({"big-alert", alert.details});
  } else {
    pendingSamples.push_back({"weather-alerts", "No Current Alerts"});
    pendingSamples.push_back({"headline-alert", "No Current Alerts"});
    pendingSamples.push_back({"big-alert", "No Current Alerts"});
  }
  pendingSince = now;
  publishRetryPending = false;
}

void retryPublish(uint32_t now) {
  if (pendingSamples.empty() ||
      (publishRetryPending && now - lastPublishRetryAt < kPublishRetryMs)) {
    return;
  }
  std::vector<aio_publisher::FeedSample> samples;
  samples.reserve(pendingSamples.size());
  for (const Sample& sample : pendingSamples) {
    samples.push_back({sample.key, sample.value.c_str()});
  }
  if (aio_publisher::enqueueGroup(kGroupName, samples.data(), samples.size())) {
    Serial.printf("NWS Alerts: accepted %u samples by shared publisher\n",
                  static_cast<unsigned>(samples.size()));
    pendingSamples.clear();
    publishRetryPending = false;
    snapshotPending = false;
    lastCycleAt = now;
    lastAcceptedAt = now;
    haveAccepted = true;
    if (pendingIsAlert && !alerts.empty()) {
      nextAlertIndex = (pendingAlertIndex + 1) % alerts.size();
    }
  } else {
    Serial.println(
        "NWS Alerts: retaining complete snapshot for publish-queue retry");
    publishRetryPending = true;
    lastPublishRetryAt = now;
  }
}

}  // namespace

void begin() {
  configured = validCoordinate(NWS_ALERTS_LATITUDE, -90.0, 90.0) &&
               validCoordinate(NWS_ALERTS_LONGITUDE, -180.0, 180.0);
  if (configured) {
    Serial.println(
        "NWS Alerts: enabled; HTTPS active-alert polling every 5 minutes");
  } else {
    Serial.println(
        "NWS Alerts: disabled; configure NWS_ALERTS_LATITUDE and "
        "NWS_ALERTS_LONGITUDE in include/credentials.h");
  }
}

void update(bool wifiConnected, bool timeSynchronized, uint32_t now) {
  if (!configured) {
    return;
  }
  consumeWorkerResult(now);
  retryPublish(now);
  prepareSamples(now);
  retryPublish(now);
  if (!wifiConnected || !timeSynchronized || !pendingSamples.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> guard(resultLock);
    if (workerRunning || resultPending ||
        (attempted && now - lastAttemptAt < nextAttemptDelay)) {
      return;
    }
    if (!https_gate::tryAcquire("NWS Alerts")) {
      return;
    }
    workerRunning = true;
  }
  attempted = true;
  lastAttemptAt = now;
  nextAttemptDelay = kPollIntervalMs;
  if (xTaskCreate(fetchTask, "nws-alerts", 8192, nullptr, 1, nullptr) !=
      pdPASS) {
    https_gate::release();
    std::lock_guard<std::mutex> guard(resultLock);
    workerRunning = false;
    failRequest(now, "unable to start worker", 0);
  }
}

HealthStatus health(uint32_t now) {
  if (!configured) {
    return HealthStatus::Off;
  }
  if (!pendingSamples.empty()) {
    const uint32_t waiting = now - pendingSince;
    if (waiting >= kStaleAfterMs) {
      return HealthStatus::Red;
    }
    if (waiting >= kQueueDelayAmberMs) {
      return HealthStatus::Amber;
    }
  }
  if (failedRequests >= 3) {
    return HealthStatus::Red;
  }
  if (!haveAccepted) {
    return HealthStatus::Amber;
  }
  if (now - lastAcceptedAt >= kStaleAfterMs) {
    return HealthStatus::Red;
  }
  return failedRequests ? HealthStatus::Amber : HealthStatus::Green;
}

}  // namespace nws_alerts
