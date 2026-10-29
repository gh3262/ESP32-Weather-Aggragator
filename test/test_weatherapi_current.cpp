#define CREDENTIALS_H
#define WEATHERAPI_KEY "test-weather-key"
#define LATITUDE "47.6"
#define LONGITUDE "-122.3"
#include "../src/weatherapi_current.cpp"

#include <cassert>
#include <map>
#include <string>

TestSerial Serial;
uint32_t testMillis = 0;
TestEsp ESP;
TestWiFi WiFi;
void (*testTask)(void*) = nullptr;
bool testTaskFailure = false;
int testHttpStatus = 200;
std::string testHttpBody;
unsigned testHttpCalls = 0;

namespace {
std::map<std::string, std::string> accepted;
unsigned submissions = 0;
bool acceptGroup = true;

const char completeWeather[] = R"JSON({
  "location":{"name":"Seattle","lat":47.6,"lon":-122.3},
  "current":{
    "last_updated_epoch":1792868100,"last_updated":"2026-10-28 19:15",
    "temp_f":68.9,"condition":{"text":"Sunny","code":1000},
    "wind_mph":5.8,"wind_degree":133,"wind_dir":"SE","pressure_mb":1021.0,
    "precip_in":0.0,"humidity":42,"cloud":0,"feelslike_f":64.4,
    "windchill_f":-5.5,"heatindex_f":101.2,"dewpoint_f":44.9,"gust_mph":8.4,
    "air_quality":{"co":182.0,"pm2_5":11.5,"us-epa-index":2}
  }
})JSON";

void reset() {
  using namespace weatherapi_current;
  workerResult = FetchResult{};
  workerRunning = false;
  resultPending = false;
  configured = false;
  attempted = false;
  lastAttemptAt = nextAttemptDelay = lastSuccessAt = 0;
  haveSuccess = false;
  pendingSamples.clear();
  pendingSince = lastAcceptedAt = 0;
  haveAccepted = false;
  publishRetryPending = false;
  lastPublishRetryAt = failedRequests = 0;
  https_gate::release();
  testTask = nullptr;
  testTaskFailure = false;
  testHttpStatus = HTTP_CODE_OK;
  testHttpBody = completeWeather;
  testHttpCalls = 0;
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
  using namespace weatherapi_current;
  accepted.clear();
  testTask = nullptr;
  update(true, true, at);
  assert(testTask);
  testTask(nullptr);
  update(true, true, at + 1);
}
}

namespace aio_publisher {
bool enqueueGroup(const char* name, const FeedSample* samples, size_t count) {
  assert(std::string(name) == "WeatherAPI current");
  ++submissions;
  if (!acceptGroup) {
    return false;
  }
  accepted.clear();
  for (size_t i = 0; i < count; ++i) {
    assert(accepted.emplace(samples[i].key, samples[i].value).second);
  }
  return true;
}
}

int main() {
  using namespace weatherapi_current;
  assert(kPollIntervalMs == 25UL * 60UL * 1000UL);
  assert(kStaleAfterMs == 50UL * 60UL * 1000UL);
  reset();
  begin();
  assert(configured);
  assert(health(0) == HealthStatus::Amber);

  update(true, false, 0);
  assert(testHttpCalls == 0 && !testTask);
  update(true, true, 0);
  assert(testTask && testHttpCalls == 0);
  testTask(nullptr);
  assert(testHttpCalls == 1);
  update(true, true, 1);
  assert(submissions == 1 && accepted.size() == 16);
  assert(value("wapi.aqi") == "2");
  assert(value("wapi.cond-code") == "1000");
  assert(value("wapi.desc") == "Clear");
  assert(value("wapi.as-of") == "2026-10-28 19:15");
  assert(value("wapi.clouds") == "0");
  assert(value("wapi.dew-point") == "44.9");
  assert(value("wapi.feels-like") == "64.4");
  assert(value("wapi.heat-index") == "101.2");
  assert(value("wapi.humidity") == "42");
  assert(value("wapi.precip") == "0");
  assert(value("wapi.pressure") == "1021");
  assert(value("wapi.temperature") == "68.9");
  assert(value("wapi.wind-chill") == "-5.5");
  assert(value("wapi.wind-gust") == "8.4");
  assert(value("wapi.wind-speed") == "5.8");
  assert(value("wapi.wind-dir") == "SE");
  assert(accepted.count("rt") == 0);
  assert(accepted.count("lat") == 0 && accepted.count("lon") == 0);
  assert(Serial.output.find("test-weather-key") == std::string::npos);
  assert(health(1) == HealthStatus::Green);
  assert(health(kStaleAfterMs) == HealthStatus::Green);
  assert(health(kStaleAfterMs + 1) == HealthStatus::Red);

  // Every legacy lookup entry is preserved.
  assert(sizeof(kConditions) / sizeof(kConditions[0]) == 48);
  assert(std::string(conditionText(1063)) == "Patchy rain possible");
  assert(std::string(conditionText(1282)) ==
         "Moderate or heavy snow with thunder");
  assert(conditionText(1001) == nullptr);

  update(true, true, kPollIntervalMs - 1);
  assert(testHttpCalls == 1);
  testTask = nullptr;
  update(true, true, kPollIntervalMs);
  assert(testTask);

  // Normal queue-admission delay stays green; prolonged delay escalates.
  testTask(nullptr);
  acceptGroup = false;
  const uint32_t queuedAt = kPollIntervalMs + 1;
  update(true, true, queuedAt);
  assert(!pendingSamples.empty());
  assert(Serial.output.find("retaining complete snapshot") != std::string::npos);
  assert(health(queuedAt + kQueueDelayAmberMs - 1) == HealthStatus::Green);
  assert(health(queuedAt + kQueueDelayAmberMs) == HealthStatus::Amber);
  assert(health(queuedAt + kStaleAfterMs) == HealthStatus::Red);
  acceptGroup = true;
  update(true, true, queuedAt + kPublishRetryMs - 1);
  assert(!pendingSamples.empty());
  update(true, true, queuedAt + kPublishRetryMs);
  assert(pendingSamples.empty());
  assert(health(queuedAt + kPublishRetryMs) == HealthStatus::Green);

  // Unknown codes keep the raw code and skip the description; invalid
  // fields are skipped individually.
  testHttpBody = R"JSON({"current":{
    "condition":{"code":1001},"air_quality":{"us-epa-index":7},
    "temp_f":"68","humidity":101,"cloud":-1,"precip_in":-0.1,
    "pressure_mb":0,"wind_mph":null,"gust_mph":false,"wind_degree":361,
    "last_updated":"","feelslike_f":250,"windchill_f":32,"heatindex_f":32,
    "dewpoint_f":32}})JSON";
  poll(3 * kPollIntervalMs);
  assert(value("wapi.cond-code") == "1001");
  assert(accepted.count("wapi.desc") == 0);
  assert(Serial.output.find("unknown condition code 1001") != std::string::npos);
  const char* skippedKeys[] = {
      "wapi.aqi", "wapi.temperature", "wapi.humidity", "wapi.clouds",
      "wapi.precip", "wapi.pressure", "wapi.wind-speed", "wapi.wind-gust",
      "wapi.wind-dir", "wapi.as-of", "wapi.feels-like"};
  for (const char* key : skippedKeys) {
    assert(accepted.count(key) == 0);
  }
  assert(value("wapi.wind-chill") == "32");
  assert(value("wapi.heat-index") == "32");
  assert(value("wapi.dew-point") == "32");

  testHttpBody = R"JSON({"current":{"condition":{"code":"1000"},
    "wind_degree":360,"air_quality":{"us-epa-index":6},"precip_in":0.01,
    "pressure_mb":1013.7}})JSON";
  poll(4 * kPollIntervalMs);
  assert(accepted.count("wapi.cond-code") == 0 && accepted.count("wapi.desc") == 0);
  assert(value("wapi.wind-dir") == "N");
  assert(value("wapi.aqi") == "6");
  assert(value("wapi.precip") == "0.01");
  assert(value("wapi.pressure") == "1013.7");

  // Request failures: amber while retrying, red after three in a row.
  reset();
  begin();
  poll(0);
  testHttpStatus = 500;
  uint32_t failAt = kPollIntervalMs;
  for (int failure = 1; failure <= 3; ++failure) {
    poll(failAt);
    assert(failedRequests == static_cast<uint32_t>(failure));
    assert(health(failAt + 1) ==
           (failure < 3 ? HealthStatus::Amber : HealthStatus::Red));
    failAt += kRetryDelayMs + 1;
  }

  const char* invalidBodies[] = {"{not-json", "[]", R"JSON({"current":5})JSON"};
  for (const char* body : invalidBodies) {
    reset();
    begin();
    testHttpBody = body;
    poll(0);
    assert(failedRequests == 1 && pendingSamples.empty() && submissions == 0);
    testTask = nullptr;
    update(true, true, kRetryDelayMs);
    assert(!testTask);
    update(true, true, kRetryDelayMs + 1);
    assert(testTask);
  }

  reset();
  begin();
  testHttpBody = std::string(kMaxResponseBytes + 1, ' ');
  poll(0);
  assert(failedRequests == 1);
  assert(Serial.output.find("response too large") != std::string::npos);

  // A busy shared HTTPS slot defers the poll without counting an attempt.
  reset();
  begin();
  assert(https_gate::tryAcquire());
  update(true, true, 0);
  assert(!testTask && !attempted);
  https_gate::release();
  update(true, true, 5);
  assert(testTask && attempted);
  assert(!https_gate::tryAcquire());
  testTask(nullptr);
  assert(https_gate::tryAcquire());
  https_gate::release();

  reset();
  begin();
  testTaskFailure = true;
  update(true, true, 0);
  assert(failedRequests == 1 && https_gate::tryAcquire());
  https_gate::release();

  reset();
  begin();
  update(false, true, 0);
  assert(!testTask);
  poll(UINT32_MAX - 100);
  const uint32_t wrappedPoll = UINT32_MAX - 100 + kPollIntervalMs;
  testTask = nullptr;
  update(true, true, wrappedPoll - 1);
  assert(testHttpCalls == 1 && !testTask);
  update(true, true, wrappedPoll);
  assert(testTask);

  return 0;
}
