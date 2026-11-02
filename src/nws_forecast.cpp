#include "nws_forecast.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
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

namespace nws_forecast {
namespace {
constexpr uint32_t kPollIntervalMs = 60UL * 60UL * 1000UL;
constexpr uint32_t kDiscoveryIntervalMs = 24UL * kPollIntervalMs;
constexpr uint32_t kStartupDelayMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kRetryDelayMs = 60000;
constexpr uint32_t kPublishRetryMs = 2000;
constexpr uint32_t kStaleAfterMs = 2UL * kPollIntervalMs;
constexpr size_t kMaxResponseBytes = 32768;
constexpr size_t kMaxNarrativeBytes = 505;
constexpr char kGroupName[] = "NWS Forecast";
constexpr char kUserAgent[] = "ESP32-Weather-Aggregator/1.0 (NWS forecasts)";
constexpr const char* kKeys[] = {
    "nws.00", "nws.01", "nws.02", "nws.03", "nws.04", "nws.05", "nws.06",
    "nws.07", "nws.08", "nws.09", "nws.10", "nws.11", "nws.12", "nws.13"};

struct Sample {
  const char* key;
  std::string value;
};
struct FetchResult {
  int status = 0;
  bool overflow = false;
  bool readFailed = false;
  bool allocationFailed = false;
  std::unique_ptr<char[]> body;
  size_t length = 0;
};
struct LocalTime {
  unsigned year, month, day, hour, minute, second;
};

std::mutex resultLock;
FetchResult workerResult;
bool workerRunning = false;
bool resultPending = false;
bool configured = false;
bool readyStarted = false;
uint32_t readyAt = 0;
bool attempted = false;
uint32_t lastAttemptAt = 0;
uint32_t nextAttemptDelay = 0;
std::string forecastUrl;
std::string workerUrl;
bool workerDiscovery = false;
bool haveDiscovery = false;
uint32_t lastDiscoveryAt = 0;
std::vector<Sample> pendingSamples;
uint32_t pendingSince = 0;
bool publishRetryPending = false;
uint32_t lastPublishRetryAt = 0;
bool haveAccepted = false;
uint32_t lastAcceptedAt = 0;
uint32_t failedRequests = 0;

class BoundedBody : public Stream {
 public:
  explicit BoundedBody(char* buffer) : data(buffer) {}
  char* data;
  size_t length = 0;
  bool overflow = false;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t size) override {
    const size_t copied = size < kMaxResponseBytes - length ?
                          size : kMaxResponseBytes - length;
    memcpy(data + length, bytes, copied);
    length += copied;
    data[length] = '\0';
    overflow = overflow || copied != size;
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

bool validCoordinate(const char* text, double minimum, double maximum) {
  if (!text || !text[0]) return false;
  char* end = nullptr;
  const double value = strtod(text, &end);
  return end != text && *end == '\0' && std::isfinite(value) &&
         value >= minimum && value <= maximum;
}

std::string pointUrl() {
  // NWS redirects higher-precision coordinates; request supported precision.
  char coordinates[64];
  snprintf(coordinates, sizeof(coordinates), "%.4f,%.4f",
           strtod(LATITUDE, nullptr), strtod(LONGITUDE, nullptr));
  return std::string("https://api.weather.gov/points/") + coordinates;
}

bool validForecastUrl(const char* url) {
  const char prefix[] = "https://api.weather.gov/gridpoints/";
  if (!url || strncmp(url, prefix, sizeof(prefix) - 1)) return false;
  const char* cursor = url + sizeof(prefix) - 1;
  for (int i = 0; i < 3; ++i) {
    if (*cursor < 'A' || *cursor > 'Z') return false;
    ++cursor;
  }
  if (*cursor++ != '/') return false;
  for (char delimiter : {',', '/'}) {
    const char* start = cursor;
    while (*cursor >= '0' && *cursor <= '9') ++cursor;
    if (cursor == start || cursor - start > 4 || *cursor++ != delimiter) return false;
  }
  return !strcmp(cursor, "forecast");
}

bool parseTime(const char* text, LocalTime& output) {
  if (!text || strlen(text) != 25) return false;
  for (size_t i = 0; i < 25; ++i) {
    const char separator = i == 4 || i == 7 ? '-' : i == 10 ? 'T' :
                           i == 13 || i == 16 || i == 22 ? ':' : '\0';
    if (i == 19) {
      if (text[i] != '+' && text[i] != '-') return false;
    } else if (separator ? text[i] != separator : text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  output = {static_cast<unsigned>(atoi(text)),
            static_cast<unsigned>(atoi(text + 5)),
            static_cast<unsigned>(atoi(text + 8)),
            static_cast<unsigned>(atoi(text + 11)),
            static_cast<unsigned>(atoi(text + 14)),
            static_cast<unsigned>(atoi(text + 17))};
  if (output.year < 1970 || output.month < 1 || output.month > 12 ||
      output.hour > 23 || output.minute > 59 || output.second > 59 ||
      atoi(text + 20) > 23 || atoi(text + 23) > 59) return false;
  const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = output.year % 4 == 0 &&
                    (output.year % 100 != 0 || output.year % 400 == 0);
  return output.day >= 1 &&
         output.day <= days[output.month - 1] + (output.month == 2 && leap ? 1 : 0);
}

std::string hourText(const LocalTime& time) {
  char text[24];
  const unsigned hour = time.hour % 12 ? time.hour % 12 : 12;
  if (time.minute) {
    snprintf(text, sizeof(text), "%u:%02u%s", hour, time.minute,
             time.hour < 12 ? "am" : "pm");
  } else {
    snprintf(text, sizeof(text), "%u%s", hour, time.hour < 12 ? "am" : "pm");
  }
  return text;
}

bool textField(JsonVariantConst input, size_t maximum, std::string& value,
               bool allowEmpty = false) {
  if (!input.is<const char*>()) return false;
  const char* text = input.as<const char*>();
  const size_t length = strlen(text);
  if ((!length && !allowEmpty) || length > maximum) return false;
  value.assign(text, length);
  return true;
}

bool parseForecast(char* body, size_t length, std::vector<Sample>& samples) {
  JsonDocument filter;
  JsonObject period = filter["properties"]["periods"].add<JsonObject>();
  for (const char* field : {"name", "startTime", "endTime", "isDaytime",
                           "temperature", "temperatureUnit", "windSpeed",
                           "windDirection", "detailedForecast"}) {
    period[field] = true;
  }
  JsonDocument document;
  const auto error = deserializeJson(document, body, length,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8));
  const JsonArrayConst periods = document["properties"]["periods"].as<JsonArrayConst>();
  if (error || periods.isNull() || periods.size() == 0) {
    Serial.printf("NWS Forecast ERROR: invalid periods (%s)\n",
                  error ? error.c_str() : "empty/missing array");
    return false;
  }
  std::vector<Sample> parsed;
  static const char* const months[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const size_t count = periods.size() < 14 ? periods.size() : 14;
  for (size_t i = 0; i < count; ++i) {
    const JsonVariantConst p = periods[i];
    std::string name, unit, wind, direction, details;
    LocalTime start{}, end{};
    if (!textField(p["name"], 64, name) ||
        !textField(p["temperatureUnit"], 1, unit) || unit != "F" ||
        !textField(p["windSpeed"], 64, wind) ||
        !textField(p["windDirection"], 32, direction, true) ||
        (direction.empty() && wind != "0 mph") ||
        !textField(p["detailedForecast"], kMaxResponseBytes, details) ||
        !parseTime(p["startTime"].as<const char*>(), start) ||
        !parseTime(p["endTime"].as<const char*>(), end) ||
        !p["isDaytime"].is<bool>() || p["temperature"].is<bool>() ||
        !p["temperature"].is<int>() ||
        p["temperature"].as<int>() < -200 || p["temperature"].as<int>() > 200) {
      Serial.printf("NWS Forecast ERROR: invalid period %u; snapshot retained\n",
                    static_cast<unsigned>(i));
      return false;
    }
    std::string narrative = name + " " + months[start.month - 1] + " " +
        std::to_string(start.day) + ", " + hourText(start) + " to " + hourText(end) +
        " - " + (p["isDaytime"].as<bool>() ? "high near " : "low near ") +
        std::to_string(p["temperature"].as<int>()) + "F with winds: " +
        (direction.empty() ? wind : direction + " at " + wind) + ". \n" + details;
    if (narrative.size() > kMaxNarrativeBytes) {
      const size_t originalSize = narrative.size();
      constexpr char marker[] = " [truncated]";
      size_t cut = kMaxNarrativeBytes - (sizeof(marker) - 1);
      // Do not split a UTF-8 character at the byte limit.
      while (cut && (static_cast<unsigned char>(narrative[cut]) & 0xC0) == 0x80) {
        --cut;
      }
      narrative.resize(cut);
      narrative += marker;
      Serial.printf("NWS Forecast: %s truncated from %u to %u bytes\n",
                    kKeys[i], static_cast<unsigned>(originalSize),
                    static_cast<unsigned>(narrative.size()));
    }
    parsed.push_back({kKeys[i], std::move(narrative)});
  }
  // Clear obsolete positions when NWS supplies fewer than fourteen periods.
  for (size_t i = count; i < 14; ++i) {
    parsed.push_back({kKeys[i], "No forecast available"});
  }
  samples = std::move(parsed);
  Serial.printf("NWS Forecast: parsed %u periods into 14 feeds\n",
                static_cast<unsigned>(count));
  return true;
}

bool parseDiscovery(char* body, size_t length, std::string& url) {
  JsonDocument filter;
  filter["properties"]["forecast"] = true;
  JsonDocument document;
  const auto error = deserializeJson(document, body, length,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8));
  const char* discovered = document["properties"]["forecast"].as<const char*>();
  if (error || !validForecastUrl(discovered)) {
    Serial.printf("NWS Forecast ERROR: invalid point discovery (%s)\n",
                  error ? error.c_str() : "missing/invalid forecast URL");
    return false;
  }
  url = discovered;
  return true;
}

void fetchTask(void*) {
  FetchResult result;
  {
    WiFiClientSecure transport;
    transport.setCACert(letsencrypt_root_ca::kTrustedRootCa);
    transport.setHandshakeTimeout(5);
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(4000);
    if (http.begin(transport, workerUrl.c_str())) {
      http.addHeader("User-Agent", kUserAgent);
      http.addHeader("Accept", "application/geo+json");
      result.status = http.GET();
      if (result.status == HTTP_CODE_OK) {
        result.body.reset(new (std::nothrow) char[kMaxResponseBytes + 1]);
        if (!result.body) {
          result.allocationFailed = true;
        } else {
          BoundedBody body(result.body.get());
          const int read = http.writeToStream(&body);
          result.overflow = body.overflow;
          result.readFailed = read < 0;
          result.length = body.length;
        }
      }
      http.end();
    }
  }
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
  lastAttemptAt = now;
  nextAttemptDelay = kRetryDelayMs;
  Serial.printf("NWS Forecast ERROR: request failed (%s, HTTP=%d); retry in 60 seconds\n",
                reason, status);
}

void consumeResult(uint32_t now) {
  FetchResult result;
  {
    std::lock_guard<std::mutex> guard(resultLock);
    if (!resultPending) return;
    result = std::move(workerResult);
    workerResult = FetchResult{};
    resultPending = false;
  }
  if (result.status != HTTP_CODE_OK || !result.body || !result.length ||
      result.overflow || result.readFailed || result.allocationFailed) {
    if (!workerDiscovery && (result.status == 404 || result.status == 410)) {
      haveDiscovery = false;
    }
    failRequest(now, result.allocationFailed ? "unable to allocate response" :
        result.overflow ? "response too large" :
        result.readFailed ? "response read failed" : "transport/status", result.status);
    return;
  }
  if (workerDiscovery) {
    if (!parseDiscovery(result.body.get(), result.length, forecastUrl)) {
      failRequest(now, "invalid point response", result.status);
      return;
    }
    haveDiscovery = true;
    lastDiscoveryAt = now;
    nextAttemptDelay = 0;
    Serial.printf("NWS Forecast: discovered %s\n", forecastUrl.c_str());
    return;
  }
  std::vector<Sample> samples;
  if (!parseForecast(result.body.get(), result.length, samples)) {
    failRequest(now, "invalid forecast response", result.status);
    return;
  }
  failedRequests = 0;
  nextAttemptDelay = kPollIntervalMs;
  pendingSamples = std::move(samples);
  pendingSince = now;
  publishRetryPending = false;
  Serial.println("NWS Forecast: HTTPS request succeeded; 14 samples ready for paced publication");
}

void retryPublish(uint32_t now) {
  if (pendingSamples.empty() ||
      (publishRetryPending && now - lastPublishRetryAt < kPublishRetryMs)) return;
  std::vector<aio_publisher::FeedSample> samples;
  for (const Sample& sample : pendingSamples) {
    samples.push_back({sample.key, sample.value.c_str()});
  }
  if (aio_publisher::enqueueGroup(kGroupName, samples.data(), samples.size())) {
    pendingSamples.clear();
    publishRetryPending = false;
    haveAccepted = true;
    lastAcceptedAt = now;
    Serial.println("NWS Forecast: accepted 14 samples by shared publisher");
  } else {
    if (!publishRetryPending) {
      Serial.println("NWS Forecast: retaining complete snapshot for publish-queue retry");
    }
    publishRetryPending = true;
    lastPublishRetryAt = now;
  }
}
}

void begin() {
  configured = validCoordinate(LATITUDE, -90, 90) &&
               validCoordinate(LONGITUDE, -180, 180);
  Serial.println(configured ?
      "NWS Forecast: enabled; hourly polls after 15-minute startup delay and backlog drain" :
      "NWS Forecast: disabled; configure LATITUDE and LONGITUDE");
}

void update(bool wifiConnected, bool timeSynchronized, uint32_t now) {
  if (!configured) return;
  consumeResult(now);
  retryPublish(now);
  if (!wifiConnected || !timeSynchronized || !pendingSamples.empty()) return;
  if (!readyStarted) {
    readyStarted = true;
    readyAt = now;
  }
  if (!attempted &&
      (now - readyAt < kStartupDelayMs || !aio_publisher::idle())) return;
  {
    std::lock_guard<std::mutex> guard(resultLock);
    if (workerRunning || resultPending ||
        (attempted && now - lastAttemptAt < nextAttemptDelay)) return;
    if (!https_gate::tryAcquire("NWS Forecast")) return;
    workerDiscovery = !haveDiscovery || now - lastDiscoveryAt >= kDiscoveryIntervalMs;
    workerUrl = workerDiscovery ? pointUrl() : forecastUrl;
    workerRunning = true;
  }
  attempted = true;
  lastAttemptAt = now;
  nextAttemptDelay = kPollIntervalMs;
  if (xTaskCreate(fetchTask, "nws_forecast", 8192, nullptr, 1, nullptr) != pdPASS) {
    https_gate::release();
    std::lock_guard<std::mutex> guard(resultLock);
    workerRunning = false;
    failRequest(now, "unable to start worker", 0);
  }
}

HealthStatus health(uint32_t now) {
  if (!configured) return HealthStatus::Off;
  if (failedRequests >= 3 ||
      (haveAccepted && now - lastAcceptedAt >= kStaleAfterMs) ||
      (!pendingSamples.empty() && now - pendingSince >= kStaleAfterMs)) {
    return HealthStatus::Red;
  }
  if (!haveAccepted || failedRequests ||
      (!pendingSamples.empty() && now - pendingSince >= 10UL * 60UL * 1000UL)) {
    return HealthStatus::Amber;
  }
  return HealthStatus::Green;
}
}
