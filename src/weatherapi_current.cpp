#include "weatherapi_current.h"

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

#include "aio_publisher.h"
#include "https_gate.h"
#include "weather_transforms.h"
#include "letsencrypt_root_ca.h"

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

namespace weatherapi_current {
namespace {
// Five minutes shorter than OpenWeather so the two REST polls drift apart.
constexpr uint32_t kPollIntervalMs = 25UL * 60UL * 1000UL;
constexpr uint32_t kRetryDelayMs = 60UL * 1000UL;
constexpr uint32_t kPublishRetryMs = 2000;
constexpr uint32_t kStaleAfterMs = 2UL * kPollIntervalMs;
constexpr uint32_t kQueueDelayAmberMs = 10UL * 60UL * 1000UL;
constexpr size_t kMaxResponseBytes = 4096;
constexpr char kGroupName[] = "WeatherAPI current";

struct Sample {
  const char* key;
  std::string value;
};

struct FetchResult {
  bool overflow = false;
  int status = 0;
  std::string body;
};

struct ConditionText {
  unsigned code;
  const char* text;
};

// Preserved verbatim from the legacy CircuitPython WAPI lookup table.
constexpr ConditionText kConditions[] = {
    {1000, "Clear"},
    {1003, "Partly cloudy"},
    {1006, "Cloudy"},
    {1009, "Overcast"},
    {1030, "Mist"},
    {1063, "Patchy rain possible"},
    {1066, "Patchy snow possible"},
    {1069, "Patchy sleet possible"},
    {1072, "Patchy freezing drizzle possible"},
    {1087, "Thundery outbreaks possible"},
    {1114, "Blowing snow"},
    {1117, "Blizzard"},
    {1135, "Fog"},
    {1147, "Freezing fog"},
    {1150, "Patchy light drizzle"},
    {1153, "Light drizzle"},
    {1168, "Freezing drizzle"},
    {1171, "Heavy freezing drizzle"},
    {1180, "Patchy light rain"},
    {1183, "Light rain"},
    {1186, "Moderate rain at times"},
    {1189, "Moderate rain"},
    {1192, "Heavy rain at times"},
    {1195, "Heavy rain"},
    {1198, "Light freezing rain"},
    {1201, "Moderate or heavy freezing rain"},
    {1204, "Light sleet"},
    {1207, "Moderate or heavy sleet"},
    {1210, "Patchy light snow"},
    {1213, "Light snow"},
    {1216, "Patchy moderate snow"},
    {1219, "Moderate snow"},
    {1222, "Patchy heavy snow"},
    {1225, "Heavy snow"},
    {1237, "Ice pellets"},
    {1240, "Light rain shower"},
    {1243, "Moderate or heavy rain shower"},
    {1246, "Torrential rain shower"},
    {1249, "Light sleet showers"},
    {1252, "Moderate or heavy sleet showers"},
    {1255, "Light snow showers"},
    {1258, "Moderate or heavy snow showers"},
    {1261, "Light showers of ice pellets"},
    {1264, "Moderate or heavy showers of ice pellets"},
    {1273, "Patchy light rain with thunder"},
    {1276, "Moderate or heavy rain with thunder"},
    {1279, "Patchy light snow with thunder"},
    {1282, "Moderate or heavy snow with thunder"},
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

bool integer(JsonVariantConst input, double minimum, double maximum,
             unsigned& value) {
  double raw = 0;
  if (!numeric(input, raw) || raw < minimum || raw > maximum ||
      std::trunc(raw) != raw) {
    return false;
  }
  value = static_cast<unsigned>(raw);
  return true;
}

void skipped(const char* key) {
  Serial.printf("WeatherAPI: skipped missing/invalid field for %s\n", key);
}

// ArduinoJson may store provider decimals as float (8.4 -> 8.400001), so six
// significant digits restores the provider's text for weather-scale values.
bool rawNumber(double value, std::string& output) {
  char textBuffer[48];
  const int length = snprintf(textBuffer, sizeof(textBuffer), "%.6g", value);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(textBuffer)) {
    return false;
  }
  output = textBuffer;
  return true;
}

bool temperature(double value, std::string& output) {
  return value >= -200.0 && value <= 200.0 && rawNumber(value, output);
}

bool nonnegative(double value, std::string& output) {
  return value >= 0.0 && rawNumber(value, output);
}

bool percent(double value, std::string& output) {
  return value >= 0.0 && value <= 100.0 && rawNumber(value, output);
}

bool pressure(double value, std::string& output) {
  return value > 0.0 && rawNumber(value, output);
}

void addNumeric(std::vector<Sample>& samples, JsonVariantConst input,
                const char* key, bool (*convert)(double, std::string&)) {
  double value = 0;
  std::string formatted;
  if (numeric(input, value) && convert(value, formatted)) {
    samples.push_back({key, formatted});
  } else {
    skipped(key);
  }
}

void addText(std::vector<Sample>& samples, JsonVariantConst input,
             const char* key) {
  if (!input.is<const char*>()) {
    skipped(key);
    return;
  }
  const char* raw = input.as<const char*>();
  const size_t length = strlen(raw);
  if (!length || length > 128) {
    skipped(key);
    return;
  }
  samples.push_back({key, std::string(raw, length)});
}

const char* conditionText(unsigned code) {
  for (const ConditionText& entry : kConditions) {
    if (entry.code == code) {
      return entry.text;
    }
  }
  return nullptr;
}

bool parseResponse(const std::string& body, std::vector<Sample>& samples) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, body, DeserializationOption::NestingLimit(8));
  if (error || !document.is<JsonObject>() ||
      !document["current"].is<JsonObject>()) {
    Serial.printf("WeatherAPI ERROR: invalid JSON/object (%s)\n",
                  error ? error.c_str() : "missing current object");
    return false;
  }

  const JsonObjectConst current = document["current"].as<JsonObjectConst>();
  unsigned value = 0;
  if (integer(current["air_quality"]["us-epa-index"], 1, 6, value)) {
    samples.push_back({"wapi.aqi", std::to_string(value)});
  } else {
    skipped("wapi.aqi");
  }

  if (integer(current["condition"]["code"], 0, 9999, value)) {
    samples.push_back({"wapi.cond-code", std::to_string(value)});
    const char* description = conditionText(value);
    if (description) {
      samples.push_back({"wapi.desc", description});
    } else {
      Serial.printf("WeatherAPI: unknown condition code %u; skipped wapi.desc\n",
                    value);
    }
  } else {
    skipped("wapi.cond-code");
    skipped("wapi.desc");
  }

  addText(samples, current["last_updated"], "wapi.as-of");
  addNumeric(samples, current["cloud"], "wapi.clouds", percent);
  addNumeric(samples, current["dewpoint_f"], "wapi.dew-point", temperature);
  addNumeric(samples, current["feelslike_f"], "wapi.feels-like", temperature);
  addNumeric(samples, current["heatindex_f"], "wapi.heat-index", temperature);
  addNumeric(samples, current["humidity"], "wapi.humidity", percent);
  addNumeric(samples, current["precip_in"], "wapi.precip", nonnegative);
  addNumeric(samples, current["pressure_mb"], "wapi.pressure", pressure);
  addNumeric(samples, current["temp_f"], "wapi.temperature", temperature);
  addNumeric(samples, current["windchill_f"], "wapi.wind-chill", temperature);
  addNumeric(samples, current["gust_mph"], "wapi.wind-gust", nonnegative);
  addNumeric(samples, current["wind_mph"], "wapi.wind-speed", nonnegative);

  double degrees = 0;
  const weather::CompassResult compass =
      numeric(current["wind_degree"], degrees)
          ? weather::degreesToCompass(degrees)
          : weather::CompassResult{nullptr, weather::TransformError::NotFinite};
  if (compass.ok()) {
    samples.push_back({"wapi.wind-dir", compass.value});
  } else {
    skipped("wapi.wind-dir");
  }

  Serial.printf("WeatherAPI: parsed %u weather outputs\n",
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
      std::string("https://api.weatherapi.com/v1/current.json?key=") +
      WEATHERAPI_KEY + "&q=" + LATITUDE + "," + LONGITUDE + "&aqi=yes";
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
  Serial.printf("WeatherAPI ERROR: request failed (%s, HTTP=%d); retry in 60 seconds\n",
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
  Serial.printf("WeatherAPI: HTTPS request succeeded; %u samples ready for paced publication\n",
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
    Serial.printf("WeatherAPI: accepted %u samples by shared publisher\n",
                  static_cast<unsigned>(samples.size()));
    pendingSamples.clear();
    publishRetryPending = false;
    lastAcceptedAt = now;
    haveAccepted = true;
  } else {
    Serial.println("WeatherAPI: retaining complete snapshot for publish-queue retry");
    publishRetryPending = true;
    lastPublishRetryAt = now;
  }
}

}

void begin() {
  configured = validApiKey(WEATHERAPI_KEY) &&
               validCoordinate(LATITUDE, -90.0, 90.0) &&
               validCoordinate(LONGITUDE, -180.0, 180.0);
  if (configured) {
    Serial.println("WeatherAPI: enabled; HTTPS current-weather polling every 25 minutes");
  } else {
    Serial.println("WeatherAPI: disabled; configure WEATHERAPI_KEY, LATITUDE, and LONGITUDE in include/credentials.h");
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
    if (!https_gate::tryAcquire("WeatherAPI")) {
      return;
    }
    workerRunning = true;
  }
  attempted = true;
  lastAttemptAt = now;
  nextAttemptDelay = kPollIntervalMs;
  if (xTaskCreate(fetchTask, "weatherapi", 8192, nullptr, 1, nullptr) != pdPASS) {
    https_gate::release();
    std::lock_guard<std::mutex> guard(resultLock);
    workerRunning = false;
    failRequest(now, "unable to start worker", 0);
  }
}

// Same rule as OpenWeather: successful polls accepted by the shared publisher
// are green; normal queue-admission delays and in-flight requests stay green.
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
