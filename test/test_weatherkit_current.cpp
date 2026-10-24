#include "../src/weatherkit_current.cpp"

#include <cassert>
#include <map>

TestSerial Serial;
namespace {
std::map<std::string, std::string> submitted;
size_t submissions = 0;
bool acceptGroup = true;
const char payload[] = R"JSON({
  "asOf":"2026-10-24T15:00:00Z",
  "cloudCover":0.75,"cloudCoverLowAltPct":0.25,
  "cloudCoverMidAltPct":0.50,"cloudCoverHighAltPct":1,
  "conditionCode":"MostlyClear","daylight":true,"humidity":0.65,
  "precipitationIntensity":0,"pressure":1013.25,"pressureTrend":"steady",
  "temperature":20,"temperatureApparent":19,"temperatureDewPoint":10,
  "visibility":16093.44,"windDirection":225,"windGust":32.18688,
  "windSpeed":16.09344,"uvIndex":3
})JSON";

void reset() {
  using namespace weatherkit_current;
  incoming.clear();
  assembly.clear();
  expectedTotal = 0;
  assembling = false;
  receiveFailures = 0;
  retryPending = false;
  waitingSamples.clear();
  lastRetryAt = 0;
  submissions = 0;
  submitted.clear();
  acceptGroup = true;
  Serial.output.clear();
}

void deliver(const std::string& text) {
  weatherkit_current::receive(true, text.data(), text.size(), 0, text.size());
}
}

namespace aio_publisher {
bool enqueueGroup(const char* name, const FeedSample* samples, size_t count) {
  assert(std::string(name) == "WeatherKit current");
  ++submissions;
  if (!acceptGroup) {
    return false;
  }
  submitted.clear();
  for (size_t i = 0; i < count; ++i) {
    assert(submitted.emplace(samples[i].key, samples[i].value).second);
  }
  return true;
}
}

int main() {
  using namespace weatherkit_current;
  reset();
  const size_t length = strlen(payload);
  receive(true, payload, 17, 0, length);
  update(0);
  assert(submissions == 0);
  receive(false, payload + 17, length - 17, 17, length);
  update(1);
  assert(submissions == 1 && submitted.size() == 20);
  assert(submitted.at("currentasof") == "2026-10-24T15:00:00Z");
  assert(submitted.at("currentcloudcover") == "75");
  assert(submitted.at("currentcloudcoverlowaltpct") == "25");
  assert(submitted.at("currentcloudcovermidaltpct") == "50");
  assert(submitted.at("currentcloudcoverhighaltpct") == "100");
  assert(submitted.at("currentconditioncode") == "MostlyClear");
  assert(submitted.at("currentdaylight") == "Its light");
  assert(submitted.at("currenthumidity") == "65");
  assert(submitted.at("currentprecipitationintensity") == "0");
  assert(submitted.at("currentpressure") == "1013.25");
  assert(submitted.at("currentpressuretrend") == "steady");
  assert(submitted.at("currenttemperature") == "68.0");
  assert(submitted.at("currenttemperatureapparent") == "66.2");
  assert(submitted.at("currenttemperaturedewpoint") == "50.0");
  assert(submitted.at("currentvisibility") == "10");
  assert(submitted.at("currentwinddegrees") == "225");
  assert(submitted.at("currentwinddirection") == "SW");
  assert(submitted.at("currentwindgust") == "20");
  assert(submitted.at("currentwindspeed") == "10");
  assert(submitted.at("currentuv") == "3");

  reset();
  deliver(R"({"temperature":-10,"daylight":false,"humidity":0,"windSpeed":0})");
  update(0);
  assert(submitted.size() == 4);
  assert(submitted.at("currenttemperature") == "14.0");
  assert(submitted.at("currentdaylight") == "Its dark");
  assert(submitted.at("currenthumidity") == "0");
  assert(submitted.at("currentwindspeed") == "0");
  assert(Serial.output.find("skipped missing/invalid field pressure") != std::string::npos);

  const char* invalid[] = {
      "{bad", "[]", "null", "{}",
      R"({"temperature":1e400,"windSpeed":1e400})",
      R"({"temperature":null,"humidity":"0.5","daylight":0,"pressure":true})",
      R"({"temperature":-274,"humidity":1.1,"pressure":0,"visibility":-1,
          "windDirection":361,"windGust":-1,"uvIndex":-1})",
      R"({"current":{"temperature":20}})"};
  for (const char* text : invalid) {
    reset();
    deliver(text);
    update(0);
    assert(submissions == 0 && !Serial.output.empty());
  }

  reset();
  receive(false, payload, length, 0, length);
  update(0);
  assert(submissions == 0);
  receive(true, payload, 17, 0, length);
  receive(false, payload + 18, length - 18, 18, length);
  update(1);
  assert(submissions == 0 && Serial.output.find("discarded") != std::string::npos);

  reset();
  receive(true, payload, 17, 0, length);
  disconnected();
  receive(false, payload + 17, length - 17, 17, length);
  update(0);
  assert(submissions == 0 && Serial.output.find("discarded") != std::string::npos);
  deliver(payload);
  update(1);
  assert(submissions == 1);

  reset();
  std::string oversized(4097, 'x');
  deliver(oversized);
  update(0);
  assert(submissions == 0 && Serial.output.find("discarded") != std::string::npos);
  for (size_t i = 0; i < 5; ++i) {
    deliver(payload);
  }
  assert(incoming.size() == 4);
  update(1);
  assert(Serial.output.find("discarded") != std::string::npos);

  reset();
  acceptGroup = false;
  deliver(payload);
  update(UINT32_MAX - 1000);
  assert(submissions == 1 && waitingSamples.size() == 20);
  deliver(R"({"temperature":21})");
  update(998);
  assert(submissions == 1 && incoming.size() == 1);
  acceptGroup = true;
  update(999);
  assert(submissions == 2 && submitted.at("currenttemperature") == "68.0");
  update(1000);
  assert(submissions == 3 && submitted.at("currenttemperature") == "69.8");

  reset();
  const Mapping custom[] = {
      {false, "missing", "disabled.feed", Conversion::Raw},
      {false, "windDirection", "currentwinddegrees", Conversion::Bearing},
      {true, "windDirection", "custom.compass", Conversion::Compass},
      {true, "temperature", "custom.temperature", Conversion::Temperature}};
  std::vector<Sample> selected;
  assert(parse(payload, selected, custom, 4));
  assert(selected.size() == 2);
  assert(std::string(selected[0].key) == "custom.compass" && selected[0].value == "SW");
  assert(std::string(selected[1].key) == "custom.temperature" &&
         selected[1].value == "68.0");
  assert(Serial.output.find("missing") == std::string::npos);
  const Mapping degreesOnly[] = {
      {true, "windDirection", "degrees.only", Conversion::Bearing}};
  selected.clear();
  assert(parse(payload, selected, degreesOnly, 1));
  assert(selected.size() == 1 && selected[0].value == "225");
  const Mapping disabled[] = {
      {false, "temperature", "disabled", Conversion::Temperature}};
  selected.clear();
  assert(parse(payload, selected, disabled, 1));
  assert(selected.empty());
  assert(Serial.output.find("no output feeds enabled") != std::string::npos);

  puts("WeatherKit current tests passed.");
}
