#include "openmeteo_current.h"

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
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "aio_publisher.h"
#include "https_gate.h"
#include "letsencrypt_root_ca.h"
#include "weather_transforms.h"

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

namespace openmeteo_current {
namespace {
constexpr uint32_t kPollIntervalMs = 20UL * 60UL * 1000UL;
constexpr uint32_t kRetryDelayMs = 60UL * 1000UL;
constexpr uint32_t kPublishRetryMs = 2000;
constexpr uint32_t kStaleAfterMs = 2UL * kPollIntervalMs;
constexpr uint32_t kQueueDelayAmberMs = 10UL * 60UL * 1000UL;
constexpr size_t kMaxResponseBytes = 4096;
constexpr char kGroupName[] = "Open-Meteo current";

struct Sample {
  const char* key;
  std::string value;
};

struct FetchResult {
  bool overflow = false;
  bool readFailed = false;
  bool allocationFailed = false;
  int status = 0;
  std::string body;
};

struct ConditionText {
  unsigned code;
  const char* text;
};

constexpr ConditionText kConditions[] = {
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

enum Field {
  Temp, Humidity, FeelsLike, IsDay, Precip, Rain, Showers, Snow,
  Code, Clouds, Pressure, Speed, Direction, Gust, FieldCount
};

struct FieldSpec {
  const char* name;
  const char* unit;
  double minimum;
  double maximum;
  bool integral;
};

constexpr FieldSpec kFields[] = {
    {"temperature_2m", "\xC2\xB0" "F", -200, 200, false},
    {"relative_humidity_2m", "%", 0, 100, false},
    {"apparent_temperature", "\xC2\xB0" "F", -200, 200, false},
    {"is_day", "", 0, 1, true},
    {"precipitation", "inch", 0, 1000, false},
    {"rain", "inch", 0, 1000, false},
    {"showers", "inch", 0, 1000, false},
    {"snowfall", "inch", 0, 1000, false},
    {"weather_code", "wmo code", 0, 999, true},
    {"cloud_cover", "%", 0, 100, false},
    {"pressure_msl", "hPa", 1, 2000, false},
    {"wind_speed_10m", "mp/h", 0, 1000, false},
    {"wind_direction_10m", "\xC2\xB0", 0, 360, false},
    {"wind_gusts_10m", "mp/h", 0, 1000, false}};
static_assert(sizeof(kFields) / sizeof(kFields[0]) == FieldCount,
              "Every current field needs validation");

std::mutex resultLock;
FetchResult workerResult;
bool workerRunning = false;
bool resultPending = false;
bool configured = false;
bool attempted = false;
uint32_t lastAttemptAt = 0;
uint32_t nextAttemptDelay = 0;
std::vector<Sample> pendingSamples;
uint32_t pendingSince = 0;
uint32_t lastAcceptedAt = 0;
bool haveAccepted = false;
bool publishRetryPending = false;
uint32_t lastPublishRetryAt = 0;
uint32_t failedRequests = 0;

class BoundedBody : public Stream {
 public:
  char data[kMaxResponseBytes + 1] = {};
  size_t length = 0;
  bool overflow = false;

  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t size) override {
    const size_t copied = size < kMaxResponseBytes - length
                              ? size : kMaxResponseBytes - length;
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

bool numeric(JsonVariantConst input, double& value) {
  if (input.is<bool>() || !input.is<double>()) {
    return false;
  }
  std::string serialized;
  serializeJson(input, serialized);
  char* end = nullptr;
  value = strtod(serialized.c_str(), &end);
  return end != serialized.c_str() && *end == '\0' && std::isfinite(value);
}

std::string rawNumber(double value) {
  // Match the existing providers' formatting without float storage artifacts.
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%.6g", value == 0 ? 0.0 : value);
  return buffer;
}

bool unitIs(JsonVariantConst input, const char* expected) {
  return input.is<const char*>() &&
         strcmp(input.as<const char*>(), expected) == 0;
}

bool validTime(const char* text) {
  if (!text || strlen(text) != 16) {
    return false;
  }
  for (size_t i = 0; i < 16; ++i) {
    const char separator = i == 4 || i == 7 ? '-' :
                           i == 10 ? 'T' : i == 13 ? ':' : '\0';
    if (separator ? text[i] != separator : text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  const int year = atoi(text);
  const int month = atoi(text + 5);
  const int day = atoi(text + 8);
  const int hour = atoi(text + 11);
  const int minute = atoi(text + 14);
  if (year < 1970 || month < 1 || month > 12 || hour > 23 || minute > 59) {
    return false;
  }
  const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day >= 1 && day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
}

std::string friendlyTime(const char* timestamp) {
  static const char* const months[] = {
      "January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December"};
  const int hour = atoi(timestamp + 11);
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "On %d %s at %d:%02d %s",
           atoi(timestamp + 8), months[atoi(timestamp + 5) - 1],
           hour % 12 == 0 ? 12 : hour % 12, atoi(timestamp + 14),
           hour < 12 ? "am" : "pm");
  return buffer;
}

const char* conditionText(unsigned code) {
  for (const ConditionText& entry : kConditions) {
    if (entry.code == code) {
      return entry.text;
    }
  }
  return nullptr;
}

const char* windText(double degrees) {
  const char* point = weather::degreesToCompass(degrees).value;
  if (!strcmp(point, "N")) return "North";
  if (!strcmp(point, "E")) return "East";
  if (!strcmp(point, "S")) return "South";
  if (!strcmp(point, "W")) return "West";
  return point;
}

bool parseResponse(const std::string& body, std::vector<Sample>& samples) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, body, DeserializationOption::NestingLimit(8));
  if (error || !document.is<JsonObject>() ||
      !document["current"].is<JsonObject>() ||
      !document["current_units"].is<JsonObject>()) {
    Serial.printf("Open-Meteo ERROR: invalid JSON/object (%s)\n",
                  error ? error.c_str() : "missing current/current_units");
    return false;
  }
  const JsonObjectConst current = document["current"].as<JsonObjectConst>();
  const JsonObjectConst units = document["current_units"].as<JsonObjectConst>();
  const char* timestamp = current["time"].as<const char*>();
  double interval = 0;
  if (!validTime(timestamp) || !unitIs(units["time"], "iso8601") ||
      !unitIs(units["interval"], "seconds") ||
      !numeric(current["interval"], interval) || interval <= 0 ||
      interval > 3600 || std::trunc(interval) != interval) {
    Serial.println("Open-Meteo ERROR: invalid current time/interval or units");
    return false;
  }
  double values[FieldCount] = {};
  std::string text[FieldCount];
  for (size_t i = 0; i < FieldCount; ++i) {
    const FieldSpec& field = kFields[i];
    if (!unitIs(units[field.name], field.unit) ||
        !numeric(current[field.name], values[i]) ||
        values[i] < field.minimum || values[i] > field.maximum ||
        (field.integral && std::trunc(values[i]) != values[i])) {
      Serial.printf("Open-Meteo ERROR: missing/invalid field or units: %s\n",
                    field.name);
      return false;
    }
    text[i] = rawNumber(values[i]);
  }

  const char* direction = windText(values[Direction]);
  const std::string winds = "Winds: " + text[Speed] + "mp/h from the " +
                            direction + " gusting " + text[Gust];
  samples = {
      {"om-when", timestamp}, {"om-winds", winds},
      {"om-windspeed", text[Speed]}, {"om-windgust", text[Gust]},
      {"om-totprec", text[Precip]}, {"om-rain", text[Rain]},
      {"om-showers", text[Showers]}, {"om-snow", text[Snow]},
      {"om-hum", text[Humidity]}, {"om-clouds", text[Clouds]},
      {"om-temp", text[Temp]}, {"om-fl", text[FeelsLike]},
      {"om-pres", text[Pressure]}};
  const char* description = conditionText(static_cast<unsigned>(values[Code]));
  if (description) {
    samples.push_back({"om-desc", description});
    const std::string precip = values[Precip] < 0.01 ? "No precipitation" :
        "Total Precip " + text[Precip] + "inches Rain: " + text[Rain] +
        "inches Showers: " + text[Showers] + "inches Snow: " + text[Snow] + "inches.";
    const std::string summary =
        friendlyTime(timestamp) + ", " +
        (values[IsDay] == 1 ? "Daytime" : "Nighttime") + ",  it is " +
        description + ",  Temp " + text[Temp] + "\xC2\xB0" "F, Humidity " +
        text[Humidity] + "%,  Feels Like " + text[FeelsLike] + "\xC2\xB0" "F, " +
        precip + ",  Clouds " + text[Clouds] + "%, Barometer " + text[Pressure] +
        " hPa, Wind Speed " + text[Speed] + "mp/h, from the " + direction +
        ", gusting " + text[Gust] + "mp/h";
    if (summary.size() > 512) {
      Serial.println("Open-Meteo ERROR: current summary exceeds publisher limit");
      samples.clear();
      return false;
    }
    samples.push_back({"om-current", summary});
  } else {
    Serial.printf("Open-Meteo: unknown WMO code %u; skipped om-desc and om-current\n",
                  static_cast<unsigned>(values[Code]));
  }
  Serial.printf("Open-Meteo: parsed %u current outputs; pressure trend and history disabled\n",
                static_cast<unsigned>(samples.size()));
  return true;
}

FetchResult fetchWeather() {
  FetchResult result;
  WiFiClientSecure transport;
  transport.setCACert(letsencrypt_root_ca::kTrustedRootCa);
  transport.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  const std::string url =
      std::string("https://api.open-meteo.com/v1/forecast?latitude=") + LATITUDE +
      "&longitude=" + LONGITUDE +
      "&timezone=auto&temperature_unit=fahrenheit&wind_speed_unit=mph"
      "&precipitation_unit=inch&forecast_days=1"
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
      "precipitation,rain,showers,snowfall,weather_code,cloud_cover,pressure_msl,"
      "wind_speed_10m,wind_direction_10m,wind_gusts_10m";
  if (http.begin(transport, url.c_str())) {
    result.status = http.GET();
    if (result.status == HTTP_CODE_OK) {
      BoundedBody* body = new (std::nothrow) BoundedBody();
      if (!body) {
        result.allocationFailed = true;
      } else {
        const int read = http.writeToStream(body);
        result.overflow = body->overflow;
        result.readFailed = read < 0;
        if (!result.overflow && !result.readFailed) {
          result.body.assign(body->data, body->length);
        }
        delete body;
      }
    }
    http.end();
  }
  return result;
}

void fetchTask(void*) {
  FetchResult result = fetchWeather();
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
  lastAttemptAt = now;
  Serial.printf("Open-Meteo ERROR: request failed (%s, HTTP=%d); retry in 60 seconds\n",
                reason, status);
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
  if (result.status != HTTP_CODE_OK || result.overflow || result.readFailed ||
      result.allocationFailed || result.body.empty()) {
    const char* reason = result.allocationFailed ? "unable to allocate response" :
                         result.overflow ? "response too large" :
                         result.readFailed ? "response read failed" : "transport/status";
    failRequest(now, reason, result.status);
    return;
  }
  std::vector<Sample> samples;
  if (!parseResponse(result.body, samples)) {
    failRequest(now, "invalid response", result.status);
    return;
  }
  failedRequests = 0;
  nextAttemptDelay = kPollIntervalMs;
  pendingSamples = std::move(samples);
  pendingSince = now;
  publishRetryPending = false;
  Serial.printf("Open-Meteo: HTTPS request succeeded; %u samples ready for paced publication\n",
                static_cast<unsigned>(pendingSamples.size()));
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
    Serial.printf("Open-Meteo: accepted %u samples by shared publisher\n",
                  static_cast<unsigned>(samples.size()));
    pendingSamples.clear();
    publishRetryPending = false;
    lastAcceptedAt = now;
    haveAccepted = true;
  } else {
    Serial.println("Open-Meteo: retaining complete snapshot for publish-queue retry");
    publishRetryPending = true;
    lastPublishRetryAt = now;
  }
}
}

void begin() {
  configured = validCoordinate(LATITUDE, -90.0, 90.0) &&
               validCoordinate(LONGITUDE, -180.0, 180.0);
  if (configured) {
    Serial.println("Open-Meteo: enabled; HTTPS current-weather polling every 20 minutes");
  } else {
    Serial.println("Open-Meteo: disabled; configure LATITUDE and LONGITUDE in include/credentials.h");
  }
}

void update(bool wifiConnected, bool timeSynchronized, uint32_t now) {
  if (!configured) {
    return;
  }
  consumeWorkerResult(now);
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
    if (!https_gate::tryAcquire("Open-Meteo")) {
      return;
    }
    workerRunning = true;
  }
  attempted = true;
  lastAttemptAt = now;
  nextAttemptDelay = kPollIntervalMs;
  if (xTaskCreate(fetchTask, "openmeteo", 8192, nullptr, 1, nullptr) != pdPASS) {
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
}
