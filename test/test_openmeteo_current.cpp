#define CREDENTIALS_H
#define LATITUDE "47.6"
#define LONGITUDE "-122.3"
#include "../src/openmeteo_current.cpp"
#include "../src/temperature_average.cpp"

#include <cassert>
#include <map>

TestSerial Serial;
uint32_t testMillis = 0;
TestEsp ESP;
void (*testTask)(void*) = nullptr;
bool testTaskFailure = false;
int testHttpStatus = 200;
std::string testHttpBody;
unsigned testHttpCalls = 0;
bool testHttpBegin = true;
bool testHttpReadFailure = false;

namespace {
std::map<std::string, std::string> accepted;
unsigned submissions = 0;
bool acceptGroup = true;

const char completeWeather[] = R"JSON({
  "utc_offset_seconds":-25200,"timezone":"America/Los_Angeles",
  "current_units":{
    "time":"iso8601","interval":"seconds","temperature_2m":"\u00b0F",
    "relative_humidity_2m":"%","apparent_temperature":"\u00b0F","is_day":"",
    "precipitation":"inch","rain":"inch","showers":"inch","snowfall":"inch",
    "weather_code":"wmo code","cloud_cover":"%","pressure_msl":"hPa",
    "wind_speed_10m":"mp/h","wind_direction_10m":"\u00b0","wind_gusts_10m":"mp/h"
  },
  "current":{
    "time":"2026-10-31T17:45","interval":900,"temperature_2m":68.9,
    "relative_humidity_2m":42,"apparent_temperature":64.4,"is_day":1,
    "precipitation":0.15,"rain":0.1,"showers":0.05,"snowfall":0,
    "weather_code":63,"cloud_cover":85,"pressure_msl":1021.4,
    "wind_speed_10m":5.8,"wind_direction_10m":133,"wind_gusts_10m":8.4
  }
})JSON";

void reset() {
  using namespace openmeteo_current;
  temperature_average::begin();
  workerResult = FetchResult{};
  workerRunning = resultPending = configured = attempted = false;
  lastAttemptAt = nextAttemptDelay = 0;
  pendingSamples.clear();
  pendingSince = lastAcceptedAt = 0;
  haveAccepted = publishRetryPending = false;
  lastPublishRetryAt = failedRequests = 0;
  https_gate::release();
  testTask = nullptr;
  testTaskFailure = false;
  testHttpStatus = HTTP_CODE_OK;
  testHttpBody = completeWeather;
  testHttpCalls = 0;
  testHttpBegin = true;
  testHttpReadFailure = false;
  testMillis = 0;
  accepted.clear();
  submissions = 0;
  acceptGroup = true;
  Serial.output.clear();
}

const std::string& value(const char* key) {
  const auto found = accepted.find(key);
  assert(found != accepted.end());
  return found->second;
}

void poll(uint32_t at) {
  using namespace openmeteo_current;
  testTask = nullptr;
  update(true, true, at);
  assert(testTask);
  testTask(nullptr);
  assert(!https_gate::busyFlag().load());
  testTask = nullptr;
  update(true, true, at + 1);
}

std::string changed(const char* section, const char* field,
                    const char* replacement) {
  JsonDocument doc;
  assert(!deserializeJson(doc, completeWeather));
  JsonDocument replacementDoc;
  assert(!deserializeJson(replacementDoc, replacement));
  doc[section][field] = replacementDoc.as<JsonVariantConst>();
  std::string result;
  serializeJson(doc, result);
  return result;
}
}

namespace aio_publisher {
bool enqueueGroup(const char* name, const FeedSample* samples, size_t count) {
  assert(std::string(name) == "Open-Meteo current");
  ++submissions;
  if (!acceptGroup) {
    return false;
  }
  accepted.clear();
  for (size_t i = 0; i < count; ++i) {
    assert(std::string(samples[i].value).size() <= 512);
    assert(accepted.emplace(samples[i].key, samples[i].value).second);
  }
  return true;
}
}

int main() {
  using namespace openmeteo_current;
  assert(kPollIntervalMs == 20UL * 60UL * 1000UL);
  assert(kStaleAfterMs == 40UL * 60UL * 1000UL);
  assert(!validCoordinate("", -90, 90));
  assert(!validCoordinate("nan", -90, 90));
  assert(!validCoordinate("91", -90, 90));
  assert(!validCoordinate("35x", -90, 90));
  assert(validCoordinate("-90", -90, 90));
  reset();
  begin();
  assert(configured && health(0) == HealthStatus::Amber);
  update(false, true, 0);
  assert(!testTask);
  update(true, false, 0);
  assert(!testTask);
  poll(0);
  assert(temperature_average::inputs[0].fresh);
  assert(temperature_average::inputs[0].value == 68.9);
  assert(temperature_average::inputs[0].receivedAt == 1);
  assert(submissions == 1 && accepted.size() == 15);
  assert(value("om-when") == "2026-10-31T17:45");
  assert(value("om-winds") == "Winds: 5.8mp/h from the SE gusting 8.4");
  assert(value("om-windspeed") == "5.8");
  assert(value("om-windgust") == "8.4");
  assert(value("om-totprec") == "0.15");
  assert(value("om-rain") == "0.1");
  assert(value("om-showers") == "0.05");
  assert(value("om-snow") == "0");
  assert(value("om-hum") == "42");
  assert(value("om-clouds") == "85");
  assert(value("om-temp") == "68.9");
  assert(value("om-fl") == "64.4");
  assert(value("om-pres") == "1021.4");
  assert(value("om-desc") == "Rain");
  assert(value("om-current") ==
      "On 31 October at 5:45 pm, Daytime,  it is Rain,  Temp 68.9\xC2\xB0" "F, Humidity 42%, "
      " Feels Like 64.4\xC2\xB0" "F, Total Precip 0.15inches Rain: 0.1inches "
      "Showers: 0.05inches Snow: 0inches.,  Clouds 85%, Barometer 1021.4 hPa, "
      "Wind Speed 5.8mp/h, from the SE, gusting 8.4mp/h");
  assert(!accepted.count("om-pres-trend") && !accepted.count("rt"));
  assert(!accepted.count("om-past") && !accepted.count("lat") && !accepted.count("lon"));
  assert(health(1) == HealthStatus::Green);
  assert(health(kStaleAfterMs + 1) == HealthStatus::Red);
  update(true, true, kPollIntervalMs - 1);
  assert(!testTask);
  const auto first = accepted;
  poll(kPollIntervalMs);
  assert(accepted == first);
  assert(temperature_average::inputs[0].receivedAt == kPollIntervalMs + 1);

  acceptGroup = false;
  poll(2 * kPollIntervalMs);
  const uint32_t queuedAt = 2 * kPollIntervalMs + 1;
  assert(pendingSamples.size() == 15);
  assert(temperature_average::inputs[0].receivedAt == queuedAt);
  assert(Serial.output.find("retaining complete snapshot") != std::string::npos);
  assert(health(queuedAt + kQueueDelayAmberMs - 1) == HealthStatus::Green);
  assert(health(queuedAt + kQueueDelayAmberMs) == HealthStatus::Amber);
  assert(health(queuedAt + kStaleAfterMs) == HealthStatus::Red);
  acceptGroup = true;
  update(true, true, queuedAt + kPublishRetryMs - 1);
  assert(pendingSamples.size() == 15);
  update(true, true, queuedAt + kPublishRetryMs);
  assert(pendingSamples.empty() && accepted == first);
  assert(temperature_average::inputs[0].receivedAt == queuedAt);

  // Wet-to-dry values are explicitly zero without restarting.
  JsonDocument dry;
  assert(!deserializeJson(dry, completeWeather));
  dry["current"]["precipitation"] = 0;
  dry["current"]["rain"] = 0;
  dry["current"]["showers"] = 0;
  dry["current"]["snowfall"] = 0;
  dry["current"]["is_day"] = 0;
  dry["current"]["wind_direction_10m"] = 360;
  dry["current"]["weather_code"] = 0;
  dry["current"]["time"] = "2026-11-01T01:15";
  dry["utc_offset_seconds"] = -28800;
  testHttpBody.clear();
  serializeJson(dry, testHttpBody);
  poll(3 * kPollIntervalMs);
  for (const char* key : {"om-totprec", "om-rain", "om-showers", "om-snow"}) {
    assert(value(key) == "0");
  }
  assert(value("om-desc") == "Clear sky");
  assert(value("om-when") == "2026-11-01T01:15");
  assert(value("om-current").find("On 1 November at 1:15 am, ") == 0);
  assert(value("om-current").find("Nighttime") != std::string::npos);
  assert(value("om-current").find("No precipitation") != std::string::npos);
  assert(value("om-winds").find("North") != std::string::npos);
  assert(std::string(conditionText(99)) == "Thunderstorms with heavy hail");
  // Keep the longest supported description and largest validated values bounded.
  JsonDocument largest;
  assert(!deserializeJson(largest, completeWeather));
  for (size_t i = 0; i < FieldCount; ++i) {
    largest["current"][kFields[i].name] = kFields[i].maximum;
  }
  largest["current"]["weather_code"] = 95;
  std::string largestBody;
  serializeJson(largest, largestBody);
  std::vector<Sample> largestSamples;
  assert(parseResponse(largestBody, largestSamples));
  assert(largestSamples.size() == 15);
  for (const Sample& sample : largestSamples) {
    assert(sample.value.size() <= 512);
  }
  testHttpBody = changed("current", "weather_code", "999");
  poll(4 * kPollIntervalMs);
  assert(accepted.size() == 13 && !accepted.count("om-desc") &&
         !accepted.count("om-current"));
  assert(Serial.output.find("unknown WMO code 999") != std::string::npos);

  // A malformed snapshot never replaces the last good values.
  struct InvalidField { const char* section; const char* field; const char* json; };
  const InvalidField invalid[] = {
      {"current", "temperature_2m", "\"68\""},
      {"current", "temperature_2m", "null"},
      {"current", "relative_humidity_2m", "101"},
      {"current", "cloud_cover", "-1"},
      {"current", "rain", "-0.01"},
      {"current", "is_day", "true"},
      {"current", "is_day", "0.5"},
      {"current", "weather_code", "63.5"},
      {"current", "wind_direction_10m", "361"},
      {"current", "pressure_msl", "0"},
      {"current", "interval", "0"},
      {"current", "time", "\"2026-02-30T12:00\""},
      {"current", "time", "\"2026-11-01T24:00\""},
      {"current_units", "snowfall", "\"cm\""},
      {"current_units", "temperature_2m", "\"\\u00b0C\""},
      {"current_units", "wind_speed_10m", "\"km/h\""}};
  for (const InvalidField& field : invalid) {
    reset();
    begin();
    poll(0);
    const auto good = accepted;
    testHttpBody = changed(field.section, field.field, field.json);
    poll(kPollIntervalMs);
    assert(failedRequests == 1 && submissions == 1 && accepted == good);
    assert(temperature_average::inputs[0].receivedAt == 1);
    assert(health(kPollIntervalMs + 1) == HealthStatus::Amber);
    assert(Serial.output.find("Open-Meteo ERROR:") != std::string::npos);
  }
  for (const char* body : {"{bad-json", "[]", "{\"current\":{}}", ""}) {
    reset();
    begin();
    testHttpBody = body;
    poll(0);
    assert(failedRequests == 1 && submissions == 0);
    update(true, true, kRetryDelayMs);
    assert(!testTask);
    update(true, true, kRetryDelayMs + 1);
    assert(testTask);
  }
  reset();
  begin();
  poll(0);
  testHttpStatus = 429;
  uint32_t failAt = kPollIntervalMs;
  for (unsigned failure = 1; failure <= 3; ++failure) {
    poll(failAt);
    assert(failedRequests == failure);
    assert(health(failAt + 1) ==
           (failure < 3 ? HealthStatus::Amber : HealthStatus::Red));
    failAt += kRetryDelayMs + 1;
  }
  testHttpStatus = 200;
  poll(failAt);
  assert(failedRequests == 0 && health(failAt + 1) == HealthStatus::Green);

  reset();
  begin();
  testHttpBody.append(kMaxResponseBytes - testHttpBody.size(), ' ');
  poll(0);
  assert(failedRequests == 0 && accepted.size() == 15);
  reset();
  begin();
  testHttpBody = std::string(kMaxResponseBytes + 1, ' ');
  poll(0);
  assert(failedRequests == 1 && submissions == 0);
  assert(Serial.output.find("response too large") != std::string::npos);
  reset();
  begin();
  testHttpReadFailure = true;
  poll(0);
  assert(failedRequests == 1 && submissions == 0);
  assert(Serial.output.find("response read failed") != std::string::npos);
  reset();
  begin();
  testHttpBegin = false;
  poll(0);
  assert(failedRequests == 1 && testHttpCalls == 0);

  reset();
  begin();
  assert(https_gate::tryAcquire("other"));
  update(true, true, 0);
  assert(!testTask && !attempted && failedRequests == 0);
  https_gate::release();
  testTaskFailure = true;
  update(true, true, 0);
  assert(!https_gate::busyFlag().load() && failedRequests == 1);
  testTaskFailure = false;
  poll(kRetryDelayMs);

  reset();
  begin();
  const uint32_t nearWrap = UINT32_MAX - 1000;
  poll(nearWrap);
  update(true, true, nearWrap + kPollIntervalMs - 1);
  assert(!testTask);
  poll(nearWrap + kPollIntervalMs);
  assert(testHttpCalls == 2 && health(nearWrap + kPollIntervalMs + 1) ==
         HealthStatus::Green);
  assert(validTime("2028-02-29T12:00") && !validTime("2027-02-29T12:00"));
  assert(friendlyTime("2026-01-01T00:05") == "On 1 January at 12:05 am");
  assert(friendlyTime("2026-12-31T12:00") == "On 31 December at 12:00 pm");
  assert(friendlyTime("2026-10-31T18:45") == "On 31 October at 6:45 pm");
  assert(friendlyTime("2028-02-29T23:09") == "On 29 February at 11:09 pm");
  Serial.println("Open-Meteo current tests passed");
  std::puts("Open-Meteo current tests passed");
}
