#define CREDENTIALS_H
#define LATITUDE "35.1548152"
#define LONGITUDE "-89.7277686"
#include "../src/nws_forecast.cpp"

#include <cassert>
#include <fstream>
#include <iterator>
#include <map>

TestSerial Serial;
uint32_t testMillis = 0;
TestEsp ESP;
void (*testTask)(void*) = nullptr;
bool testTaskFailure = false;
int testHttpStatus = 200;
std::string testHttpBody;
std::string testHttpUrl;
unsigned testHttpCalls = 0;
bool testReadFailure = false;

namespace {
bool publisherIdle = true;
bool acceptGroup = true;
unsigned submissions = 0;
std::map<std::string, std::string> accepted;
const char discovery[] = R"({"properties":{"forecast":"https://api.weather.gov/gridpoints/MEG/54,68/forecast"}})";

std::string forecast(size_t count = 14) {
  JsonDocument doc;
  JsonArray periods = doc["properties"]["periods"].to<JsonArray>();
  for (size_t i = 0; i < count; ++i) {
    JsonObject p = periods.add<JsonObject>();
    p["name"] = i % 2 ? "Tonight" : "Today";
    p["startTime"] = i % 2 ? "2026-11-01T18:00:00-06:00" : "2026-11-01T12:00:00-06:00";
    p["endTime"] = i % 2 ? "2026-11-02T00:00:00-06:00" : "2026-11-01T18:00:00-06:00";
    p["isDaytime"] = i % 2 == 0;
    p["temperature"] = i % 2 ? -5 : 70;
    p["temperatureUnit"] = "F";
    p["windSpeed"] = "5 to 10 mph";
    p["windDirection"] = "NE";
    p["detailedForecast"] = "Mostly sunny.";
  }
  std::string output;
  serializeJson(doc, output);
  return output;
}

void reset() {
  using namespace nws_forecast;
  workerResult = FetchResult{};
  workerRunning = resultPending = configured = readyStarted = attempted = false;
  readyAt = lastAttemptAt = nextAttemptDelay = 0;
  forecastUrl.clear();
  workerUrl.clear();
  workerDiscovery = haveDiscovery = false;
  lastDiscoveryAt = 0;
  pendingSamples.clear();
  pendingSince = lastPublishRetryAt = lastAcceptedAt = failedRequests = 0;
  publishRetryPending = haveAccepted = false;
  testTask = nullptr;
  testTaskFailure = testReadFailure = false;
  testHttpStatus = 200;
  testHttpCalls = submissions = 0;
  testHttpBody = discovery;
  publisherIdle = acceptGroup = true;
  accepted.clear();
  Serial.output.clear();
  https_gate::release();
}

void complete(uint32_t at) {
  assert(testTask);
  testTask(nullptr);
  testTask = nullptr;
  assert(!https_gate::busyFlag().load());
  nws_forecast::consumeResult(at);
  nws_forecast::retryPublish(at);
}

bool parse(std::string text, std::vector<nws_forecast::Sample>& samples) {
  std::vector<char> body(text.begin(), text.end());
  body.push_back('\0');
  return nws_forecast::parseForecast(body.data(), text.size(), samples);
}
}

namespace aio_publisher {
bool idle() { return publisherIdle; }
bool enqueueGroup(const char* name, const FeedSample* samples, size_t count) {
  assert(std::string(name) == "NWS Forecast" && count == 14);
  ++submissions;
  if (!acceptGroup) return false;
  accepted.clear();
  for (size_t i = 0; i < count; ++i) {
    assert(strlen(samples[i].value) <= 505);
    assert(accepted.emplace(samples[i].key, samples[i].value).second);
  }
  return true;
}
}

int main(int argc, char** argv) {
  using namespace nws_forecast;
  assert(kPollIntervalMs == 3600000 && kStartupDelayMs == 900000);
  assert(pointUrl() == "https://api.weather.gov/points/35.1548,-89.7278");
  for (const char* invalid : {"http://api.weather.gov/gridpoints/MEG/54,68/forecast",
       "https://api.weather.gov.evil/gridpoints/MEG/54,68/forecast",
       "https://api.weather.gov/gridpoints/MEG/54,68/forecast?x=1", "",
       "https://api.weather.gov/gridpoints/MEG/54,68/forecastHourly"}) {
    assert(!validForecastUrl(invalid));
  }
  reset();
  begin();
  update(false, true, 0);
  assert(!readyStarted);
  update(true, false, 0);
  assert(!readyStarted);
  update(true, true, 100);
  update(true, true, 100 + kStartupDelayMs - 1);
  assert(!testTask && !attempted);
  publisherIdle = false;
  update(true, true, 100 + kStartupDelayMs);
  assert(!testTask);
  publisherIdle = true;
  assert(https_gate::tryAcquire("other"));
  update(true, true, 100 + kStartupDelayMs);
  assert(!testTask && !attempted);
  https_gate::release();
  update(true, true, 100 + kStartupDelayMs);
  complete(101 + kStartupDelayMs);
  assert(testHttpUrl == pointUrl() && haveDiscovery);
  testHttpBody = forecast();
  update(true, true, 102 + kStartupDelayMs);
  complete(103 + kStartupDelayMs);
  assert(accepted.size() == 14 && health(103 + kStartupDelayMs) == HealthStatus::Green);
  assert(accepted["nws.00"] ==
      "Today Nov 1, 12pm to 6pm - high near 70F with winds: NE at 5 to 10 mph. \nMostly sunny.");
  assert(accepted["nws.01"] ==
      "Tonight Nov 1, 6pm to 12am - low near -5F with winds: NE at 5 to 10 mph. \nMostly sunny.");
  assert(!accepted.count("rt") && !accepted.count("lat"));
  const auto good = accepted;
  update(true, true, lastAttemptAt + kPollIntervalMs - 1);
  assert(!testTask);
  const uint32_t secondAt = lastAttemptAt + kPollIntervalMs;
  update(true, true, secondAt);
  acceptGroup = false;
  complete(secondAt + 1);
  assert(pendingSamples.size() == 14);
  assert(health(secondAt + 1 + 600000) == HealthStatus::Amber);
  assert(health(secondAt + 1 + kStaleAfterMs) == HealthStatus::Red);
  acceptGroup = true;
  retryPublish(secondAt + kPublishRetryMs);
  assert(!pendingSamples.empty());
  retryPublish(secondAt + 1 + kPublishRetryMs);
  assert(pendingSamples.empty() && accepted == good);

  std::vector<Sample> samples;
  assert(parse(forecast(2), samples) && samples.size() == 14);
  assert(samples[2].value == "No forecast available");
  assert(!parse(forecast(0), samples));
  for (const char* invalid : {"{bad", "[]", "{\"properties\":{\"periods\":null}}"}) {
    assert(!parse(invalid, samples));
  }
  struct Invalid { const char* key; const char* json; };
  for (const Invalid& invalid : {
      Invalid{"temperature", "\"70\""}, {"temperature", "true"},
      {"temperatureUnit", "\"C\""}, {"isDaytime", "1"},
      {"startTime", "\"2026-02-30T12:00:00-06:00\""},
      {"windSpeed", "null"}, {"detailedForecast", "\"\""}}) {
    JsonDocument doc, value;
    assert(!deserializeJson(doc, forecast()));
    assert(!deserializeJson(value, invalid.json));
    doc["properties"]["periods"][0][invalid.key] = value.as<JsonVariantConst>();
    std::string text;
    serializeJson(doc, text);
    assert(!parse(text, samples));
  }
  JsonDocument oversized;
  assert(!deserializeJson(oversized, forecast()));
  oversized["properties"]["periods"][0]["detailedForecast"] = std::string(512, 'x');
  std::string text;
  serializeJson(oversized, text);
  assert(parse(text, samples) && samples.size() == 14);
  assert(samples[0].value.size() == 505);
  assert(samples[0].value.substr(493) == " [truncated]");
  assert(samples[1].value == good.at("nws.01"));
  assert(Serial.output.find("nws.00 truncated from") != std::string::npos);
  const size_t prefixSize = good.at("nws.00").size() - strlen("Mostly sunny.");
  for (size_t length : {size_t(504), size_t(505), size_t(506)}) {
    oversized["properties"]["periods"][0]["detailedForecast"] =
        std::string(length - prefixSize, 'x');
    text.clear();
    serializeJson(oversized, text);
    assert(parse(text, samples));
    assert(samples[0].value.size() == (length > 505 ? 505 : length));
    assert((samples[0].value.find(" [truncated]") != std::string::npos) ==
           (length > 505));
  }
  oversized["properties"]["periods"][0]["detailedForecast"] =
      std::string(492 - prefixSize, 'x') + "\xE2\x80\x94" + std::string(30, 'x');
  text.clear();
  serializeJson(oversized, text);
  assert(parse(text, samples));
  assert(samples[0].value.size() == 504);
  assert(samples[0].value.substr(492) == " [truncated]");
  JsonDocument calm;
  assert(!deserializeJson(calm, forecast()));
  calm["properties"]["periods"][0]["windDirection"] = "";
  calm["properties"]["periods"][0]["windSpeed"] = "0 mph";
  text.clear();
  serializeJson(calm, text);
  assert(parse(text, samples));
  assert(samples[0].value.find("winds: 0 mph.") != std::string::npos);
  calm["properties"]["periods"][0]["windSpeed"] = "5 mph";
  text.clear();
  serializeJson(calm, text);
  assert(!parse(text, samples));
  LocalTime time{};
  assert(parseTime("2026-11-01T01:30:00-05:00", time));
  assert(hourText(time) == "1:30am");
  assert(parseTime("2026-11-01T01:30:00-06:00", time));
  assert(hourText(time) == "1:30am");
  assert(parseTime("2028-02-29T00:00:00+00:00", time));
  assert(hourText(time) == "12am");

  for (int status : {500, 404, 410}) {
    testHttpStatus = status;
    const uint32_t at = lastAttemptAt + nextAttemptDelay + 1;
    update(true, true, at);
    complete(at + 1);
    assert(accepted == good && failedRequests > 0);
    if (status != 500) assert(!haveDiscovery);
  }
  testHttpStatus = 200;
  testHttpBody = discovery;
  update(true, true, lastAttemptAt + kRetryDelayMs - 1);
  assert(!testTask);
  uint32_t at = lastAttemptAt + kRetryDelayMs + 1;
  update(true, true, at);
  complete(at + 1);
  testHttpBody = forecast();
  update(true, true, at + 2);
  complete(at + 3);
  assert(failedRequests == 0);
  at = lastDiscoveryAt + kDiscoveryIntervalMs;
  testHttpBody = discovery;
  update(true, true, at);
  assert(workerDiscovery);
  complete(at + 1);

  for (bool readFailure : {false, true}) {
    reset();
    begin();
    update(true, true, 0);
    testHttpBody = std::string(kMaxResponseBytes + 1, ' ');
    testReadFailure = readFailure;
    update(true, true, kStartupDelayMs);
    complete(kStartupDelayMs + 1);
    assert(failedRequests == 1 && submissions == 0);
  }
  reset();
  begin();
  update(true, true, 0);
  testTaskFailure = true;
  update(true, true, kStartupDelayMs);
  assert(failedRequests == 1 && !https_gate::busyFlag().load());
  reset();
  begin();
  const uint32_t nearWrap = UINT32_MAX - 1000;
  update(true, true, nearWrap);
  update(true, true, nearWrap + kStartupDelayMs - 1);
  assert(!testTask);
  update(true, true, nearWrap + kStartupDelayMs);
  complete(nearWrap + kStartupDelayMs + 1);
  assert(haveDiscovery);
  if (argc == 2) {
    std::ifstream file(argv[1], std::ios::binary);
    assert(file);
    const std::string live((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    const bool valid = parse(live, samples);
    if (!valid) std::fputs(Serial.output.c_str(), stderr);
    assert(valid && samples.size() == 14);
  }
  std::puts("NWS forecast tests passed");
}
