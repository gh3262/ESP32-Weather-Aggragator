#include <time.h>

struct tm* testGmtimeR(const time_t*, struct tm*);
#define gmtime_r testGmtimeR
#define CREDENTIALS_H
#define OPENWEATHER_KEY "test-weather-key"
#define LATITUDE "47.6"
#define LONGITUDE "-122.3"
#include "../src/openweather_current.cpp"
#undef gmtime_r

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

struct tm* testGmtimeR(const time_t* input, struct tm* output) {
  const struct tm* converted = gmtime(input);
  if (!converted) {
    return nullptr;
  }
  *output = *converted;
  return output;
}

namespace {
std::map<std::string, std::string> accepted;
unsigned submissions = 0;
bool acceptGroup = true;

const char completeWeather[] = R"JSON({
  "weather":[{"main":"Clouds","description":"broken clouds","id":803,"icon":"04d"}],
  "visibility":1609.344,
  "dt":1792854000,
  "clouds":{"all":100},
  "main":{"pressure":0,"temp":20.25,"feels_like":19,"temp_max":0,
          "temp_min":0,"humidity":0},
  "wind":{"deg":360,"speed":1,"gust":0},
  "rain":{"1h":25.4},
  "sys":{"sunrise":1792825200,"sunset":1792882800,"country":"US"},
  "timezone":-25200,
  "name":"Seattle"
})JSON";

void reset() {
  using namespace openweather_current;
  workerResult = FetchResult{};
  workerRunning = false;
  resultPending = false;
  configured = false;
  attempted = false;
  lastAttemptAt = nextAttemptDelay = lastSuccessAt = 0;
  haveSuccess = false;
  pendingSamples.clear();
  publishRetryPending = false;
  lastPublishRetryAt = failedRequests = 0;
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
}

namespace aio_publisher {
bool enqueueGroup(const char* name, const FeedSample* samples, size_t count) {
  assert(std::string(name) == "OpenWeather current");
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
  using namespace openweather_current;
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
  assert(submissions == 1 && accepted.size() == 22);
  assert(value("openweather.owmaintext") == "Clouds");
  assert(value("openweather.owdescription") == "broken clouds");
  assert(value("openweather.owweatherid") == "803");
  assert(value("openweather.owicon") == "04d");
  assert(value("openweather.owvisibility") == "1");
  assert(value("openweather.owasof") == "2026-10-24 08:00");
  assert(value("openweather.owclouds") == "100");
  assert(value("openweather.owpressure") == "0");
  assert(value("openweather.owtemp") == "68.5");
  assert(value("openweather.owfeelslike") == "66.2");
  assert(value("openweather.owmaxtemp") == "32.0");
  assert(value("openweather.owmintemp") == "32.0");
  assert(value("openweather.owwinddirection") == "N");
  assert(value("openweather.owwindspeed") == "2");
  assert(value("openweather.owwindgust") == "0");
  assert(value("openweather.owrain") == "1.00");
  assert(value("openweather.owhumidity") == "0");
  assert(value("openweather.owsunrise") == "00:00");
  assert(value("openweather.owsunset") == "16:00");
  assert(value("openweather.owcity") == "Seattle, US");
  assert(value("openweather.owtimezone") == "-7");
  assert(value("rt") == "OpenWeather");
  assert(health(1) == HealthStatus::Green);

  const uint32_t firstPoll = kPollIntervalMs;
  update(true, true, firstPoll - 1);
  assert(testHttpCalls == 1);
  update(true, true, firstPoll);
  assert(testTask);

  reset();
  begin();
  update(true, true, 0);
  testTask(nullptr);
  acceptGroup = false;
  update(true, true, 1);
  assert(submissions == 1 && !pendingSamples.empty());
  assert(Serial.output.find("retaining complete snapshot") != std::string::npos);
  acceptGroup = true;
  update(true, true, 2000);
  assert(submissions == 1 && !pendingSamples.empty());
  update(true, true, 2001);
  assert(submissions == 2 && pendingSamples.empty());

  reset();
  begin();
  update(true, true, 0);
  testTask(nullptr);
  update(true, true, 1);
  accepted.clear();
  testHttpBody = R"JSON({
    "weather":[{"main":"Rain","description":"light rain","id":500,"icon":"10d"}],
    "main":{"temp":-10,"feels_like":"10","temp_max":0,"humidity":false},
    "wind":{"speed":0,"gust":null,"deg":null},
    "visibility":null,"rain":{"1h":null},"clouds":{"all":0},
    "sys":{"country":"US"},"timezone":0,"dt":1792854000
  })JSON";
  update(true, true, kPollIntervalMs);
  testTask(nullptr);
  update(true, true, kPollIntervalMs + 1);
  assert(accepted.count("openweather.owtemp") == 1);
  assert(accepted.at("openweather.owtemp") == "14.0");
  assert(accepted.count("openweather.owfeelslike") == 0);
  assert(accepted.count("openweather.owmintemp") == 0);
  assert(accepted.count("openweather.owmaxtemp") == 1);
  assert(accepted.at("openweather.owmaxtemp") == "32.0");
  assert(accepted.count("openweather.owhumidity") == 0);
  assert(accepted.count("openweather.owwindgust") == 0);
  assert(accepted.count("openweather.owrain") == 0);
  assert(accepted.count("openweather.owclouds") == 1);
  assert(accepted.at("openweather.owclouds") == "0");
  assert(accepted.count("openweather.owwindspeed") == 1);
  assert(accepted.at("openweather.owwindspeed") == "0");
  assert(accepted.count("openweather.owwinddirection") == 0);
  assert(accepted.count("openweather.owsunrise") == 0);
  assert(accepted.count("openweather.owsunset") == 0);
  assert(accepted.at("openweather.owasof") == "2026-10-24 15:00");
  testHttpBody = R"JSON({"rain":{"1h":0}})JSON";
  update(true, true, 2 * kPollIntervalMs);
  testTask(nullptr);
  update(true, true, 2 * kPollIntervalMs + 1);
  assert(accepted.count("openweather.owrain") == 1);
  assert(accepted.at("openweather.owrain") == "0.00");

  reset();
  begin();
  update(true, true, 0);
  testHttpBody = "{not-json";
  testTask(nullptr);
  update(true, true, 1);
  assert(failedRequests == 1 && pendingSamples.empty());
  update(true, true, 60000);
  assert(testHttpCalls == 1);
  update(true, true, 60001);
  assert(testTask);

  reset();
  begin();
  update(false, true, 0);
  assert(!testTask);
  update(true, true, UINT32_MAX - 100);
  assert(testTask);
  testTask(nullptr);
  update(true, true, UINT32_MAX - 99);
  const uint32_t wrappedPoll = UINT32_MAX - 100 + kPollIntervalMs;
  update(true, true, wrappedPoll - 1);
  assert(testHttpCalls == 1);
  update(true, true, wrappedPoll);
  assert(testHttpCalls == 1 && testTask);

  return 0;
}
