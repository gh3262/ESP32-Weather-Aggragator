#include "../src/weatherkit_current.cpp"

#include <cassert>
#include <map>
#include "weatherkit_day_fixture.h"

TestSerial Serial;
uint32_t testMillis = 0;
namespace {
std::map<std::string, std::string> submitted;
std::string submittedGroup;
std::string expectedGroup = "WeatherKit current";
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
  changes.clear();
  assembly.clear();
  expectedTotal = 0;
  assembling = false;
  receiveFailures = 0;
  discardedTotal = 0;
  rawOverflow = assemblyFailures = invalidPayloads = changeOverflow = 0;
  retryPending = false;
  waitingSamples.clear();
  lastRetryAt = 0;
  testMillis = 0;
  healthStarted = false;
  everConnected = false;
  for (auto& source : sources) {
    source.received = false;
    source.lastReceivedAt = 0;
    source.acceptedValues.clear();
  }
  submissions = 0;
  submitted.clear();
  submittedGroup.clear();
  expectedGroup = "WeatherKit current";
  acceptGroup = true;
  Serial.output.clear();
}

void deliver(const std::string& text,
             weatherkit_current::Group group = weatherkit_current::Group::Current) {
  weatherkit_current::receive(group,
                             text.data(), text.size(), 0, text.size());
}
}

namespace aio_publisher {
bool enqueueGroup(const char* name, const FeedSample* samples, size_t count) {
  assert(std::string(name) == expectedGroup);
  ++submissions;
  if (!acceptGroup) {
    return false;
  }
  submitted.clear();
  submittedGroup = name;
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
  receive(Group::Current, payload, 17, 0, length);
  update(0);
  assert(submissions == 0);
  receive(Group::None, payload + 17, length - 17, 17, length);
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
  receive(Group::None, payload, length, 0, length);
  update(0);
  assert(submissions == 0);
  receive(Group::Current, payload, 17, 0, length);
  receive(Group::None, payload + 18, length - 18, 18, length);
  update(1);
  assert(submissions == 0 && Serial.output.find("discarded") != std::string::npos);

  reset();
  receive(Group::Current, payload, 17, 0, length);
  disconnected();
  receive(Group::None, payload + 17, length - 17, 17, length);
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
  for (size_t i = 0; i < kMaxPendingSnapshots + 1; ++i) {
    deliver(payload);
  }
  assert(incoming.size() == kMaxPendingSnapshots);
  update(1);
  assert(Serial.output.find("discarded") != std::string::npos);

  reset();
  acceptGroup = false;
  deliver(payload);
  update(UINT32_MAX - 1000);
  assert(submissions == 1 && waitingSamples.size() == 20);
  deliver(R"({"temperature":21})");
  update(998);
  assert(submissions == 1 && changes.size() == 1 && incoming.empty());
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

  const std::string minutePayload =
      R"({"startTime":"2026-10-24T16:45:00Z","precipitationChance":0.65,"precipitationIntensity":0.25})";
  for (const Group group : {Group::Minutes5, Group::Minutes30}) {
    reset();
    expectedGroup = group == Group::Minutes5 ? "WeatherKit m5" : "WeatherKit m30";
    const std::string prefix = group == Group::Minutes5 ? "m5" : "m30";
    receive(group, minutePayload.data(), 12, 0, minutePayload.size());
    receive(Group::None, minutePayload.data() + 12, minutePayload.size() - 12,
            12, minutePayload.size());
    update(0);
    assert(submitted.size() == 3);
    assert(submitted.at(prefix + "starttime") == "2026-10-24T16:45:00Z");
    assert(submitted.at(prefix + "precipitationchance") == "65");
    assert(submitted.at(prefix + "precipitationintensity") == "0.25");
    assert(submittedGroup == expectedGroup);

    submitted.clear();
    deliver(R"({"precipitationChance":0,"precipitationIntensity":0})", group);
    update(1);
    assert(submitted.size() == 2);
    assert(submitted.at(prefix + "precipitationchance") == "0");
    assert(submitted.at(prefix + "precipitationintensity") == "0");

    const size_t beforeInvalid = submissions;
    deliver(R"({"startTime":null,"precipitationChance":1.1,"precipitationIntensity":-1})", group);
    update(2);
    assert(submissions == beforeInvalid);
    deliver(R"({"precipitationChance":"0.5","precipitationIntensity":false})", group);
    update(3);
    assert(submissions == beforeInvalid);
    deliver("{bad", group);
    update(4);
    assert(submissions == beforeInvalid);
  }

  reset();
  expectedGroup = "WeatherKit m5";
  acceptGroup = false;
  deliver(minutePayload, Group::Minutes5);
  update(0);
  deliver(minutePayload, Group::Minutes30);
  deliver(payload);
  assert(incoming.size() == 2);
  acceptGroup = true;
  update(2000);
  assert(submittedGroup == "WeatherKit m5" && submitted.size() == 3);
  expectedGroup = "WeatherKit m30";
  update(2001);
  assert(submittedGroup == "WeatherKit m30" && submitted.size() == 3);
  expectedGroup = "WeatherKit current";
  update(2002);
  assert(submittedGroup == "WeatherKit current" && submitted.size() == 20);

  reset();
  expectedGroup = "WeatherKit hour1";
  const std::string hourly = R"({
    "forecastStart":"2026-10-24T18:00:00Z","cloudCover":0.25,
    "conditionCode":"Clear","daylight":true,"humidity":0.65,
    "precipitationAmount":0,"precipitationChance":0.5,
    "precipitationType":"rain","precipitationIntensity":0,
    "pressure":1013.25,"pressureTrend":"steady",
    "snowfallIntensity":0,"snowfallAmount":0,
    "temperature":20,"temperatureApparent":19,"temperatureDewPoint":10,
    "uvIndex":3,"visibility":16093.44,"windDirection":225,
    "windGust":32.18688,"windSpeed":16.09344
  })";
  receive(Group::Hour1, hourly.data(), 11, 0, hourly.size());
  receive(Group::None, hourly.data() + 11, hourly.size() - 11, 11, hourly.size());
  update(0);
  const std::map<std::string, std::string> expectedHourly = {
      {"hours1start", "2026-10-24T18:00:00Z"},
      {"hours1cloudcover", "25"}, {"hours1conditioncode", "Clear"},
      {"hours1daylight", "It will be light"}, {"hours1humidity", "65"},
      {"hours1precipitationamount", "0"}, {"hours1precipitationchance", "50"},
      {"hours1precipitationtype", "rain"}, {"hours1precipitationintensity", "0"},
      {"hours1pressure", "1013.25"}, {"hours1pressuretrend", "steady"},
      {"hours1snowfallintensity", "0"}, {"hours1snowfallamount", "0"},
      {"hours1temperature", "68.0"}, {"hours1temperatureapparent", "66.2"},
      {"hours1temperaturedewpoint", "50.0"}, {"hours1uvindex", "3"},
      {"hours1visibility", "10"}, {"hours1winddirection", "SW"},
      {"hours1windgust", "20"}, {"hours1windspeed", "10"}};
  assert(submitted == expectedHourly);
  deliver(R"({"daylight":false,"temperature":-10,"snowfallAmount":0})", Group::Hour1);
  update(1);
  assert(submitted.size() == 2);
  assert(submitted.at("hours1daylight") == "It will be dark");
  assert(submitted.at("hours1temperature") == "14.0");
  const size_t hourlySubmissions = submissions;
  deliver(R"({"daylight":1,"precipitationChance":2,"snowfallAmount":-1,
              "visibility":-1,"forecastStart":null})", Group::Hour1);
  update(2);
  assert(submissions == hourlySubmissions);

  reset();
  expectedGroup = "WeatherKit hour2";
  receive(Group::Hour2, hourly.data(), 11, 0, hourly.size());
  receive(Group::None, hourly.data() + 11, hourly.size() - 11, 11, hourly.size());
  update(0);
  std::map<std::string, std::string> expectedHour2;
  for (const auto& sample : expectedHourly) {
    expectedHour2.emplace("hours2" + sample.first.substr(6), sample.second);
  }
  assert(submitted == expectedHour2 && submitted.size() == 21);
  deliver(R"({"daylight":false,"temperature":-10,"snowfallAmount":0})", Group::Hour2);
  update(1);
  assert(submitted.at("hours2daylight") == "It will be dark");
  assert(submitted.at("hours2temperature") == "14.0");
  const size_t hour2Submissions = submissions;
  deliver(R"({"daylight":1,"precipitationChance":2,"visibility":-1})", Group::Hour2);
  update(2);
  assert(submissions == hour2Submissions);

  reset();
  expectedGroup = "WeatherKit hour6";
  receive(Group::Hour6, hourly.data(), 11, 0, hourly.size());
  receive(Group::None, hourly.data() + 11, hourly.size() - 11, 11, hourly.size());
  update(0);
  std::map<std::string, std::string> expectedHour6;
  for (const auto& sample : expectedHourly) {
    expectedHour6.emplace("hours6" + sample.first.substr(6), sample.second);
  }
  assert(submitted == expectedHour6 && submitted.size() == 21);
  deliver(R"({"daylight":false,"temperature":-10,"snowfallAmount":0})", Group::Hour6);
  update(1);
  assert(submitted.at("hours6daylight") == "It will be dark");
  assert(submitted.at("hours6temperature") == "14.0");
  const size_t hour6Submissions = submissions;
  deliver(R"({"daylight":1,"precipitationChance":2,"visibility":-1})", Group::Hour6);
  update(2);
  assert(submissions == hour6Submissions);

  reset();
  expectedGroup = "WeatherKit hour12";
  receive(Group::Hour12, hourly.data(), 11, 0, hourly.size());
  receive(Group::None, hourly.data() + 11, hourly.size() - 11, 11, hourly.size());
  update(0);
  std::map<std::string, std::string> expectedHour12;
  for (const auto& sample : expectedHourly) {
    expectedHour12.emplace("hours12" + sample.first.substr(6), sample.second);
  }
  assert(submitted == expectedHour12 && submitted.size() == 21);
  deliver(R"({"daylight":false,"temperature":-10,"snowfallAmount":0})", Group::Hour12);
  update(1);
  assert(submitted.at("hours12daylight") == "It will be dark");
  assert(submitted.at("hours12temperature") == "14.0");
  const size_t hour12Submissions = submissions;
  deliver(R"({"daylight":1,"precipitationChance":2,"visibility":-1})", Group::Hour12);
  update(2);
  assert(submissions == hour12Submissions);

  reset();
  expectedGroup = "WeatherKit hour24";
  receive(Group::Hour24, hourly.data(), 11, 0, hourly.size());
  receive(Group::None, hourly.data() + 11, hourly.size() - 11, 11, hourly.size());
  update(0);
  std::map<std::string, std::string> expectedHour24;
  for (const auto& sample : expectedHourly) {
    expectedHour24.emplace("hours24" + sample.first.substr(6), sample.second);
  }
  assert(submitted == expectedHour24 && submitted.size() == 21);
  deliver(R"({"daylight":false,"temperature":-10,"snowfallAmount":0})", Group::Hour24);
  update(1);
  assert(submitted.at("hours24daylight") == "It will be dark");
  assert(submitted.at("hours24temperature") == "14.0");
  const size_t hour24Submissions = submissions;
  deliver(R"({"daylight":1,"precipitationChance":2,"visibility":-1})", Group::Hour24);
  update(2);
  assert(submissions == hour24Submissions);

  reset();
  expectedGroup = "WeatherKit day1";
  const size_t dayLength = strlen(kDay1Payload);
  receive(Group::Day1, kDay1Payload, 37, 0, dayLength);
  update(0);
  assert(submissions == 0);
  receive(Group::None, kDay1Payload + 37, dayLength - 37, 37, dayLength);
  update(1);
  const std::map<std::string, std::string> expectedDay1 = {
      {"days1start", "2026-10-25T05:00:00Z"}, {"days1end", "2026-10-26T05:00:00Z"},
      {"days1conditioncode", "Clear"}, {"days1maxuvindex", "3"}, {"days1moonphase", "Full"},
      {"days1moonrise", "2026-10-25T22:00:00Z"}, {"days1moonset", "2026-10-25T10:00:00Z"},
      {"days1precipitationamount", "0"}, {"days1precipitationchance", "65"},
      {"days1precipitationtype", "clear"}, {"days1snowfallamount", "0"},
      {"days1solarmidnight", "2026-10-25T05:30:00Z"},
      {"days1solarnoon", "2026-10-25T17:30:00Z"}, {"days1sunrise", "2026-10-25T12:00:00Z"},
      {"days1sunrisecivil", "2026-10-25T11:30:00Z"},
      {"days1sunrisenautical", "2026-10-25T11:00:00Z"},
      {"days1sunriseastronomical", "2026-10-25T10:30:00Z"},
      {"days1sunset", "2026-10-25T23:00:00Z"}, {"days1sunsetcivil", "2026-10-25T23:30:00Z"},
      {"days1sunsetnautical", "2026-10-26T00:00:00Z"},
      {"days1sunsetastronomical", "2026-10-26T00:30:00Z"},
      {"days1temperaturemax", "68.0"}, {"days1temperaturemin", "50.0"},
      {"days1windgustspeedmax", "20"}, {"days1windspeedavg", "10"}, {"days1windspeedmax", "15"},
      {"days1daytimestart", "2026-10-25T12:00:00Z"},
      {"days1daytimeend", "2026-10-25T23:00:00Z"},
      {"days1daytimecloudcover", "25"}, {"days1daytimeconditioncode", "MostlyClear"},
      {"days1daytimehumidity", "65"}, {"days1daytimeprecipitationamount", "0"},
      {"days1daytimeprecipitationchance", "50"}, {"days1daytimeprecipitationtype", "rain"},
      {"days1daytimesnowfallamount", "0"}, {"days1daytimetemperaturemax", "68.0"},
      {"days1daytimetemperaturemin", "50.0"}, {"days1daytimewinddirection", "SW"},
      {"days1daytimewindgustspeedmax", "20"}, {"days1daytimewindspeed", "10"},
      {"days1daytimewindspeedmax", "15"},
      {"days1ovntstart", "2026-10-25T23:00:00Z"}, {"days1ovntend", "2026-10-26T12:00:00Z"},
      {"days1ovntcloudcover", "75"}, {"days1ovntconditioncode", "Cloudy"},
      {"days1ovnthumidity", "80"}, {"days1ovntprecipitationamount", "1.25"},
      {"days1ovntprecipitationchance", "75"}, {"days1ovntprecipitationtype", "snow"},
      {"days1ovntsnowfallamount", "2"}, {"days1ovnttemperaturemax", "32.0"},
      {"days1ovnttemperaturemin", "14.0"}, {"days1ovntwinddirection", "E"},
      {"days1ovntwindgustspeedmax", "10"}, {"days1ovntwindspeed", "0"},
      {"days1ovntwindspeedmax", "20"}};
  assert(expectedDay1.size() == 56 && submitted == expectedDay1);
  deliver(kDay1Payload, Group::Day1);
  update(2);
  assert(submissions == 1);
  // Missing celestial events and malformed subobjects cannot fabricate values.
  deliver(R"({"moonrise":null,"moonset":null,"daytimeForecast":[],
    "overnightForecast":"bad","temperatureMax":21})", Group::Day1);
  update(3);
  assert(submitted.size() == 1 && submitted.at("days1temperaturemax") == "69.8");
  const size_t daySubmissions = submissions;
  deliver(R"({"daytimeForecast":{"humidity":2,"temperatureMax":-274,
    "windDirection":361},"overnightForecast":{"snowfallAmount":-1}})", Group::Day1);
  update(4);
  assert(submissions == daySubmissions);
  // A dotted literal key is not a substitute for a nested provider object.
  deliver(R"({"daytimeForecast.humidity":0.5})", Group::Day1);
  update(5);
  assert(submissions == daySubmissions);
  deliver(R"({"overnightForecast":{"temperatureMin":-11}})", Group::Day1);
  update(6);
  assert(submitted.size() == 1 && submitted.at("days1ovnttemperaturemin") == "12.2");

  reset();
  expectedGroup = "WeatherKit day2";
  receive(Group::Day2, kDay1Payload, 37, 0, dayLength);
  update(0);
  assert(submissions == 0);
  receive(Group::None, kDay1Payload + 37, dayLength - 37, 37, dayLength);
  update(1);
  std::map<std::string, std::string> expectedDay2;
  for (const auto& sample : expectedDay1) {
    expectedDay2.emplace("days2" + sample.first.substr(5), sample.second);
  }
  assert(expectedDay2.size() == 56 && submitted == expectedDay2);
  deliver(kDay1Payload, Group::Day2);
  update(2);
  assert(submissions == 1);
  deliver(R"({"moonrise":null,"moonset":null,"daytimeForecast":[],
    "overnightForecast":"bad","temperatureMax":21})", Group::Day2);
  update(3);
  assert(submitted.size() == 1 && submitted.at("days2temperaturemax") == "69.8");
  const size_t day2Submissions = submissions;
  deliver(R"({"daytimeForecast":{"humidity":2,"temperatureMax":-274,
    "windDirection":361},"overnightForecast":{"snowfallAmount":-1}})", Group::Day2);
  update(4);
  assert(submissions == day2Submissions);
  deliver(R"({"overnightForecast":{"temperatureMin":-11}})", Group::Day2);
  update(5);
  assert(submitted.size() == 1 && submitted.at("days2ovnttemperaturemin") == "12.2");

  reset();
  assert(health(false, 0) == HealthStatus::Amber);
  assert(health(false, 29999) == HealthStatus::Amber);
  assert(health(false, 30000) == HealthStatus::Red);
  assert(health(true, 30001) == HealthStatus::Amber);
  assert(health(true, 330000) == HealthStatus::Amber);
  assert(health(true, 330001) == HealthStatus::Red);

  reset();
  assert(health(true, 0) == HealthStatus::Amber);
  // A blocked publisher must not hide fresh reception for other topics.
  acceptGroup = false;
  const char* validHealth = R"({"precipitationIntensity":0,"precipitationChance":0})";
  testMillis = 1000;
  for (const auto& source : sources) {
    deliver(validHealth, source.group);
  }
  update(1000);
  assert(health(true, 1000) == HealthStatus::Green);
  assert(health(true, 180999) == HealthStatus::Green);
  assert(health(true, 181000) == HealthStatus::Amber);
  assert(health(true, 301000) == HealthStatus::Red);
  assert(health(false, 1001) == HealthStatus::Red);
  acceptGroup = true;
  for (const auto& source : sources) {
    expectedGroup = groupName(source.group);
    update(301000);
  }
  acceptGroup = false;
  expectedGroup = "WeatherKit current";
  // Refresh the other topics, leaving day2 stale.
  testMillis = 301001;
  for (const auto& source : sources) {
    if (source.group != Group::Day2) {
      deliver(validHealth, source.group);
    }
  }
  update(301001);
  assert(health(true, 301001) == HealthStatus::Red);
  deliver("not json", Group::Day2);
  update(301002);
  assert(health(true, 301002) == HealthStatus::Red);
  deliver(validHealth, Group::Day2);
  update(301003);
  assert(health(true, 301003) == HealthStatus::Green);
  // Disabled groups do not hold the indicator amber.
  reset();
  const feed_config::Mapping disabledHealth[] = {
      {false, "temperature", "unused", Conversion::Temperature}};
  const Source saved = sources[3];
  sources[3] = Source(Group::Hour1, disabledHealth, 1);
  assert(health(true, 0) == HealthStatus::Amber);
  acceptGroup = false;
  for (const auto& source : sources) {
    if (source.group != Group::Hour1) {
      deliver(validHealth, source.group);
    }
  }
  update(0);
  assert(health(true, 0) == HealthStatus::Green);
  sources[3] = saved;

  reset();
  // Validation time must not make an old queued message appear fresh.
  assert(health(true, 0) == HealthStatus::Amber);
  acceptGroup = false;
  testMillis = 1000;
  for (const auto& source : sources) {
    deliver(validHealth, source.group);
  }
  update(301000);
  assert(health(true, 301000) == HealthStatus::Red);
  const std::vector<Source> savedSources(std::begin(sources), std::end(sources));
  for (auto& source : sources) {
    source.mappings = disabledHealth;
    source.count = 1;
  }
  assert(health(true, 301000) == HealthStatus::Off);
  for (size_t i = 0; i < savedSources.size(); ++i) {
    sources[i] = savedSources[i];
  }

  reset();
  const uint32_t nearWrap = UINT32_MAX - 1000;
  testMillis = nearWrap;
  assert(health(true, nearWrap) == HealthStatus::Amber);
  acceptGroup = false;
  for (const auto& source : sources) {
    deliver(validHealth, source.group);
  }
  update(nearWrap);
  assert(health(true, nearWrap + 179999) == HealthStatus::Green);
  assert(health(true, nearWrap + 180000) == HealthStatus::Amber);
  assert(health(true, nearWrap + 300000) == HealthStatus::Red);

  reset();
  assert(discardedMessages() == 0);
  receive(Group::Current, payload, 0, 0, 4097);
  assert(discardedMessages() == 1);
  update(0);
  assert(discardedMessages() == 1);
  deliver("not JSON");
  update(1);
  assert(discardedMessages() == 2);
  deliver(R"({"temperature":null})");
  update(2);
  assert(discardedMessages() == 3);
  testMillis = 1000;
  deliver(payload);
  update(1000);
  auto currentStatus = sourceStatus(Group::Current, 2999);
  assert(currentStatus.enabled && currentStatus.received && currentStatus.ageSeconds == 1);
  testMillis = 3000;
  deliver(payload);
  update(3000);
  assert(sourceStatus(Group::Current, 3000).ageSeconds == 0);
  assert(!sourceStatus(Group::Minutes5, 3000).received);
  assert(discardedMessages() == 3);
  receive(Group::Current, payload, 10, 0, strlen(payload));
  disconnected();
  assert(discardedMessages() == 4);

  reset();
  acceptGroup = false;
  assert(health(true, 0) == HealthStatus::Amber);
  // Thirty minutes of unchanged pushes must not fill the change backlog.
  for (uint32_t cycle = 0; cycle < 30; ++cycle) {
    testMillis = cycle * 60000;
    for (const auto& source : sources) {
      deliver(validHealth, source.group);
    }
    update(testMillis);
    assert(incoming.empty());
    assert(changes.size() == kMaxPendingSnapshots - 1);
    assert(waitingSamples.size() == 1);
    assert(discardedMessages() == 0);
    assert(health(true, testMillis) == HealthStatus::Green);
  }
  acceptGroup = true;
  for (const auto& source : sources) {
    expectedGroup = groupName(source.group);
    testMillis += 2000;
    update(testMillis);
    assert(submittedGroup == expectedGroup);
    assert(submitted.size() >= 1 && submitted.size() <= 2);
  }
  assert(changes.empty() && waitingSamples.empty());

  reset();
  acceptGroup = false;
  deliver(R"({"temperature":20,"humidity":0.5})");
  update(0);
  testMillis = 1;
  deliver(R"({"temperature":20.01})"); // Same formatted 68.0 value.
  update(1);
  assert(changes.empty());
  deliver(R"({"temperature":21})");
  deliver(R"({"precipitationChance":0.5})", Group::Minutes5);
  deliver(R"({"temperature":20})");
  update(2);
  assert(changes.size() == 3);
  assert(changes[0].group == Group::Current && changes[0].samples[0].value == "69.8");
  assert(changes[1].group == Group::Minutes5);
  assert(changes[2].group == Group::Current && changes[2].samples[0].value == "68.0");
  deliver(R"({"temperature":null})");
  update(3);
  assert(changes.size() == 3 && discardedMessages() == 1);
  acceptGroup = true;
  update(2000);
  assert(submitted.at("currenttemperature") == "68.0");
  assert(submitted.at("currenthumidity") == "50");
  update(2001);
  assert(submitted.size() == 1 && submitted.at("currenttemperature") == "69.8");
  expectedGroup = "WeatherKit m5";
  update(2002);
  assert(submitted.at("m5precipitationchance") == "50");
  expectedGroup = "WeatherKit current";
  update(2003);
  assert(submitted.size() == 1 && submitted.at("currenttemperature") == "68.0");

  reset();
  acceptGroup = false;
  deliver(R"({"temperature":0})");
  update(0);
  for (size_t i = 1; i <= kMaxPendingSnapshots; ++i) {
    deliver("{\"temperature\":" + std::to_string(i) + "}");
    update(static_cast<uint32_t>(i));
  }
  assert(changes.size() == kMaxPendingSnapshots);
  testMillis = 100;
  deliver(R"({"temperature":11})");
  update(100);
  assert(discardStatus().changeQueue == 1 && discardedMessages() == 1);
  assert(sourceStatus(Group::Current, 100).ageSeconds == 0);
  assert(sources[0].acceptedValues.at("currenttemperature") == "50.0");
  assert(Serial.output.find("changed-snapshot backlog full") != std::string::npos);
  // Losing a new transition at capacity must not mark it accepted.
  acceptGroup = true;
  update(2000);
  update(2001);
  deliver(R"({"temperature":11})");
  update(2002);
  assert(sources[0].acceptedValues.at("currenttemperature") == "51.8");
  for (uint32_t now = 2003; now < 2013; ++now) {
    update(now);
  }
  assert(submitted.at("currenttemperature") == "51.8");
  assert(changes.empty() && waitingSamples.empty());
  assert(discardStatus().rawQueue == 0 && discardStatus().assembly == 0);
  assert(discardStatus().invalid == 0);

  reset();
  receive(Group::Current, payload, 0, 0, 4097);
  for (size_t i = 0; i <= kMaxPendingSnapshots; ++i) {
    deliver(payload);
  }
  assert(discardStatus().assembly == 1);
  assert(discardStatus().rawQueue == 1);
  update(0);
  assert(discardedMessages() == 2);
  assert(discardStatus().changeQueue == 0);
  assert(changes.empty()); // Identical raw messages occupy one change entry.
  assert(submissions == 1);

  puts("WeatherKit current, minute, hourly, day1, and day2 tests passed.");
}
