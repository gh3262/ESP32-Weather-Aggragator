#include "openweather_current.h"

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
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include <time.h>

#include "aio_publisher.h"
#include "https_gate.h"
#include "openweather_root_ca.h"
#include "weather_transforms.h"

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

namespace openweather_current {
namespace {
constexpr uint32_t kPollIntervalMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kRetryDelayMs = 60UL * 1000UL;
constexpr uint32_t kPublishRetryMs = 2000;
constexpr uint32_t kStaleAfterMs = 2UL * kPollIntervalMs;
constexpr uint32_t kQueueDelayAmberMs = 10UL * 60UL * 1000UL;
constexpr size_t kMaxResponseBytes = 4096;
constexpr char kGroupName[] = "OpenWeather current";

struct Sample {
  const char* key;
  std::string value;
};

struct FetchResult {
  bool overflow = false;
  int status = 0;
  std::string body;
};

std::mutex resultLock;
FetchResult workerResult;
bool workerRunning = false;
bool resultPending = false;
bool configured = false;
bool attempted = false;
uint32_t lastAttemptAt = 0;
uint32_t nextAttemptDelay = 0;
uint32_t lastSuccessAt = 0;
bool haveSuccess = false;
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

bool validApiKey(const char* key) {
  if (!key || !key[0] || strncmp(key, "YOUR_", 5) == 0) {
    return false;
  }
  for (const char* cursor = key; *cursor; ++cursor) {
    const char c = *cursor;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') || c == '-' || c == '_')) {
      return false;
    }
  }
  return true;
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

bool text(JsonVariantConst input, std::string& value) {
  if (!input.is<const char*>()) {
    return false;
  }
  const char* raw = input.as<const char*>();
  const size_t length = strlen(raw);
  if (!length || length > 128) {
    return false;
  }
  value.assign(raw, length);
  return true;
}

void addText(std::vector<Sample>& samples, JsonVariantConst input,
             const char* key) {
  std::string value;
  if (text(input, value)) {
    samples.push_back({key, value});
  } else {
    Serial.printf("OpenWeather: skipped missing/invalid field for %s\n", key);
  }
}

void addNumeric(std::vector<Sample>& samples, JsonVariantConst input,
                const char* key, bool (*convert)(double, std::string&)) {
  double value = 0;
  std::string formatted;
  if (numeric(input, value) && convert(value, formatted)) {
    samples.push_back({key, formatted});
  } else {
    Serial.printf("OpenWeather: skipped missing/invalid field for %s\n", key);
  }
}

bool rawNumber(double value, std::string& output) {
  char textBuffer[48];
  const int length = snprintf(textBuffer, sizeof(textBuffer), "%.8g", value);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(textBuffer)) {
    return false;
  }
  output = textBuffer;
  return true;
}

bool fahrenheit(double value, std::string& output) {
  const weather::NumericResult result = weather::celsiusToFahrenheit(value);
  if (!result.ok()) {
    return false;
  }
  char formatted[32];
  const int length = snprintf(formatted, sizeof(formatted), "%.1f", result.value);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(formatted)) {
    return false;
  }
  output = formatted;
  return true;
}

bool mph(double value, std::string& output) {
  const weather::NumericResult result = weather::metersPerSecondToMph(value);
  if (!result.ok()) {
    return false;
  }
  char formatted[32];
  const int length = snprintf(formatted, sizeof(formatted), "%.0f",
                              std::trunc(result.value));
  if (length < 0 || static_cast<size_t>(length) >= sizeof(formatted)) {
    return false;
  }
  output = formatted;
  return true;
}

bool miles(double value, std::string& output) {
  const weather::NumericResult result = weather::metersToMiles(value);
  if (!result.ok()) {
    return false;
  }
  char formatted[32];
  const int length = snprintf(formatted, sizeof(formatted), "%.0f",
                              std::trunc(result.value));
  if (length < 0 || static_cast<size_t>(length) >= sizeof(formatted)) {
    return false;
  }
  output = formatted;
  return true;
}

bool inches(double value, std::string& output) {
  const weather::NumericResult result = weather::millimetersToInches(value);
  if (!result.ok()) {
    return false;
  }
  const weather::NumericResult rounded = weather::roundToDecimals(result.value, 2);
  if (!rounded.ok()) {
    return false;
  }
  char formatted[32];
  const int length = snprintf(formatted, sizeof(formatted), "%.2f", rounded.value);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(formatted)) {
    return false;
  }
  output = formatted;
  return true;
}

bool percent(double value, std::string& output) {
  if (value < 0.0 || value > 100.0) {
    return false;
  }
  char formatted[32];
  const int length = snprintf(formatted, sizeof(formatted), "%.0f",
                              std::trunc(value));
  if (length < 0 || static_cast<size_t>(length) >= sizeof(formatted)) {
    return false;
  }
  output = formatted;
  return true;
}

bool pressure(double value, std::string& output) {
  return value >= 0.0 && rawNumber(value, output);
}

bool timezoneHours(double value, std::string& output) {
  char formatted[32];
  const int length = snprintf(formatted, sizeof(formatted), "%.6f",
                              value / 3600.0);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(formatted)) {
    return false;
  }
  output = formatted;
  while (!output.empty() && output.back() == '0') {
    output.pop_back();
  }
  if (!output.empty() && output.back() == '.') {
    output.pop_back();
  }
  return !output.empty();
}

void addTimestamp(std::vector<Sample>& samples, JsonVariantConst timestamp,
                  int64_t offsetSeconds, const char* key, const char* format) {
  double raw = 0;
  if (!numeric(timestamp, raw) || raw < 0 || raw > 4102444800.0 ||
      std::trunc(raw) != raw) {
    Serial.printf("OpenWeather: skipped missing/invalid field for %s\n", key);
    return;
  }
  const int64_t adjusted = static_cast<int64_t>(raw) + offsetSeconds;
  const time_t localTimestamp = static_cast<time_t>(adjusted);
  struct tm local = {};
  if (!gmtime_r(&localTimestamp, &local)) {
    Serial.printf("OpenWeather: skipped unformattable timestamp for %s\n", key);
    return;
  }
  char formatted[32];
  if (strftime(formatted, sizeof(formatted), format, &local) == 0) {
    Serial.printf("OpenWeather: skipped unformattable timestamp for %s\n", key);
    return;
  }
  samples.push_back({key, formatted});
}

bool hasKey(JsonObjectConst object, const char* key) {
  for (JsonPairConst pair : object) {
    if (strcmp(pair.key().c_str(), key) == 0) {
      return true;
    }
  }
  return false;
}

// OpenWeather omits phenomena that did not occur, so absent rain means none.
// Explicit nulls and malformed values are still skipped.
void addRain(std::vector<Sample>& samples, JsonObjectConst root) {
  const char* key = "openweather.owrain";
  if (!hasKey(root, "rain")) {
    samples.push_back({key, "0.00"});
    return;
  }
  const JsonVariantConst rain = root["rain"];
  if (rain.is<JsonObjectConst>() && !hasKey(rain.as<JsonObjectConst>(), "1h")) {
    samples.push_back({key, "0.00"});
    return;
  }
  addNumeric(samples, rain["1h"], key, inches);
}

bool parseResponse(const std::string& body, std::vector<Sample>& samples) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, body, DeserializationOption::NestingLimit(8));
  if (error || !document.is<JsonObject>()) {
    Serial.printf("OpenWeather ERROR: invalid JSON/object (%s)\n",
                  error ? error.c_str() : "root is not an object");
    return false;
  }

  const JsonObjectConst root = document.as<JsonObjectConst>();
  const JsonVariantConst condition = root["weather"][0];
  addText(samples, condition["main"], "openweather.owmaintext");
  addText(samples, condition["description"], "openweather.owdescription");
  double value = 0;
  if (numeric(condition["id"], value) && value >= 0 &&
      value <= 9999 && std::trunc(value) == value) {
    samples.push_back({"openweather.owweatherid",
                       std::to_string(static_cast<unsigned>(value))});
  } else {
    Serial.println("OpenWeather: skipped missing/invalid field for openweather.owweatherid");
  }
  addText(samples, condition["icon"], "openweather.owicon");

  addNumeric(samples, root["visibility"], "openweather.owvisibility", miles);
  addNumeric(samples, root["clouds"]["all"], "openweather.owclouds", percent);
  addNumeric(samples, root["main"]["pressure"], "openweather.owpressure", pressure);
  addNumeric(samples, root["main"]["temp"], "openweather.owtemp", fahrenheit);
  addNumeric(samples, root["main"]["feels_like"], "openweather.owfeelslike", fahrenheit);
  addNumeric(samples, root["main"]["temp_max"], "openweather.owmaxtemp", fahrenheit);
  addNumeric(samples, root["main"]["temp_min"], "openweather.owmintemp", fahrenheit);
  addNumeric(samples, root["wind"]["speed"], "openweather.owwindspeed", mph);
  addNumeric(samples, root["wind"]["gust"], "openweather.owwindgust", mph);
  addRain(samples, root);
  addNumeric(samples, root["main"]["humidity"], "openweather.owhumidity", percent);

  const JsonVariantConst direction = root["wind"]["deg"];
  if (numeric(direction, value)) {
    const weather::CompassResult compass = weather::degreesToCompass(value);
    if (compass.ok()) {
      samples.push_back({"openweather.owwinddirection", compass.value});
    } else {
      Serial.println("OpenWeather: skipped missing/invalid field for openweather.owwinddirection");
    }
  } else {
    Serial.println("OpenWeather: skipped missing/invalid field for openweather.owwinddirection");
  }

  const JsonVariantConst offset = root["timezone"];
  int64_t offsetSeconds = 0;
  bool validOffset = numeric(offset, value) && std::trunc(value) == value &&
                     value >= -86400.0 && value <= 86400.0;
  if (validOffset) {
    offsetSeconds = static_cast<int64_t>(value);
    std::string formatted;
    if (timezoneHours(value, formatted)) {
      samples.push_back({"openweather.owtimezone", formatted});
    }
  } else {
    Serial.println("OpenWeather: skipped missing/invalid field for openweather.owtimezone");
  }
  if (validOffset) {
    addTimestamp(samples, root["dt"], offsetSeconds, "openweather.owasof",
                 "%Y-%m-%d %H:%M");
    addTimestamp(samples, root["sys"]["sunrise"], offsetSeconds,
                 "openweather.owsunrise", "%H:%M");
    addTimestamp(samples, root["sys"]["sunset"], offsetSeconds,
                 "openweather.owsunset", "%H:%M");
  } else {
    Serial.println("OpenWeather: skipped timestamps without a valid timezone");
  }

  std::string city;
  std::string country;
  if (text(root["name"], city) && text(root["sys"]["country"], country) &&
      city.size() + country.size() + 2 <= 256) {
    samples.push_back({"openweather.owcity", city + ", " + country});
  } else {
    Serial.println("OpenWeather: skipped missing/invalid field for openweather.owcity");
  }
  samples.push_back({"rt", "OpenWeather"});
  Serial.printf("OpenWeather: parsed %u weather outputs; trend feeds are not queried\n",
                static_cast<unsigned>(samples.size() - 1));
  return true;
}

FetchResult fetchWeather() {
  FetchResult result;
  WiFiClientSecure transport;
  transport.setCACert(kTrustedRootCa);
  transport.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  const std::string url =
      std::string("https://api.openweathermap.org/data/2.5/weather?lat=") +
      LATITUDE + "&lon=" + LONGITUDE + "&appid=" + OPENWEATHER_KEY +
      "&units=metric";
  if (http.begin(transport, url.c_str())) {
    result.status = http.GET();
    if (result.status == HTTP_CODE_OK) {
      BoundedBody* body = new (std::nothrow) BoundedBody();
      if (!body) {
        result.overflow = true;
      } else {
        const int read = http.writeToStream(body);
        result.overflow = body->overflow || read < 0;
        if (!result.overflow) {
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
  Serial.printf("OpenWeather ERROR: request failed (%s, HTTP=%d); retry in 60 seconds\n",
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
  if (result.status != HTTP_CODE_OK || result.overflow || result.body.empty()) {
    failRequest(now, result.overflow ? "response too large" : "transport/status",
                result.status);
    return;
  }
  std::vector<Sample> samples;
  if (!parseResponse(result.body, samples)) {
    failRequest(now, "invalid response", result.status);
    return;
  }
  lastSuccessAt = now;
  haveSuccess = true;
  failedRequests = 0;
  nextAttemptDelay = kPollIntervalMs;
  pendingSamples = std::move(samples);
  pendingSince = now;
  publishRetryPending = false;
  Serial.printf("OpenWeather: HTTPS request succeeded; %u samples ready for paced publication\n",
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
    Serial.printf("OpenWeather: accepted %u samples by shared publisher\n",
                  static_cast<unsigned>(samples.size()));
    pendingSamples.clear();
    publishRetryPending = false;
    lastAcceptedAt = now;
    haveAccepted = true;
  } else {
    Serial.println("OpenWeather: retaining complete snapshot for publish-queue retry");
    publishRetryPending = true;
    lastPublishRetryAt = now;
  }
}

}

void begin() {
  configured = validApiKey(OPENWEATHER_KEY) &&
               validCoordinate(LATITUDE, -90.0, 90.0) &&
               validCoordinate(LONGITUDE, -180.0, 180.0);
  if (configured) {
    Serial.println("OpenWeather: enabled; HTTPS current-weather polling every 30 minutes");
  } else {
    Serial.println("OpenWeather: disabled; configure OPENWEATHER_KEY, LATITUDE, and LONGITUDE in include/credentials.h");
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
    // Another REST worker owns the TLS slot; try again on a later loop.
    if (!https_gate::tryAcquire()) {
      return;
    }
    workerRunning = true;
  }
  attempted = true;
  lastAttemptAt = now;
  nextAttemptDelay = kPollIntervalMs;
  if (xTaskCreate(fetchTask, "openweather", 8192, nullptr, 1, nullptr) != pdPASS) {
    https_gate::release();
    std::lock_guard<std::mutex> guard(resultLock);
    workerRunning = false;
    failRequest(now, "unable to start worker", 0);
  }
}

// Health reflects successful polls and acceptance by the shared publisher;
// broker delivery is reported by the AIO pixel. In-flight requests and normal
// queue-admission delays remain green.
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
