#include <time.h>
time_t testTime(time_t*);
time_t testMktime(struct tm*);
struct tm* testLocaltime(const time_t*, struct tm*);
struct tm* testGmtime(const time_t*, struct tm*);
#define time testTime
#define mktime testMktime
#define localtime_r testLocaltime
#define gmtime_r testGmtime
#define CREDENTIALS_H
#define LATITUDE "47.6"
#define LONGITUDE "-122.3"
#define TIMEZONE "America/Chicago"
#include "../src/openmeteo_past.cpp"
#undef time
#undef mktime
#undef localtime_r
#undef gmtime_r

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
unsigned testHttpCalls = 0;
bool testHttpReadFailure = false;
std::string testUrl;
namespace fake_preferences {
bool failOpen = false;
bool failWrite = false;
unsigned writes = 0;
}

namespace {
time_t wallClock = 1793628000;  // 2026-11-02 14:00 UTC.
bool acceptGroup = true;
bool idlePublisher = true;
aio_publisher::HistoryAcknowledged completion = nullptr;
struct Sent { std::string key, value, createdAt; };
std::vector<Sent> sent;
std::map<std::string, std::string> summaries;
unsigned summaryAttempts = 0;

std::string fixture() {
  using namespace openmeteo_past;
  JsonDocument doc;
  doc["timezone"] = TIMEZONE;
  doc["daily_units"]["time"] = "iso8601";
  for (const Field& field : kFields) doc["daily_units"][field.name] = field.unit;
  for (const char* date : {"2026-10-26", "2026-10-27", "2026-10-28", "2026-10-29",
                           "2026-10-30", "2026-10-31", "2026-11-01"}) {
    doc["daily"]["time"].add(date);
    const double values[] = {0, 0, 0, 0, 70, 50, 40000, 20000, 0, 68, 48, 10, 225};
    for (size_t field = 0; field < kFieldCount; ++field) {
      doc["daily"][kFields[field].name].add(values[field]);
    }
  }
  std::string text;
  serializeJson(doc, text);
  return text;
}

void reset(bool preserveStorage = false) {
  using namespace openmeteo_past;
  wallClock = 1793628000;
  workerResult = FetchResult{};
  workerRunning = resultPending = false;
  configured = attempted = admissionRetry = awaitingAcknowledgement = false;
  persistenceReady = persistenceDirty = persistenceRetry = false;
  nextAttemptDelay = lastAttemptAt = admissionRetryAt = persistenceRetryAt = 0;
  dayIndex = scalarIndex = publishedCount = skippedCount = 0;
  days.clear();
  for (Record& record : records) record = Record{};
  if (!preserveStorage) fake_preferences::storage().clear();
  fake_preferences::failOpen = fake_preferences::failWrite = false;
  fake_preferences::writes = 0;
  sent.clear();
  summaries.clear();
  summaryAttempts = 0;
  completion = nullptr;
  acceptGroup = idlePublisher = true;
  https_gate::release();
  testTask = nullptr;
  testTaskFailure = testHttpReadFailure = false;
  testHttpStatus = 200;
  testHttpCalls = testMillis = 0;
  testHttpBody = fixture();
  Serial.output.clear();
  begin();
}

void poll(uint32_t now) {
  using namespace openmeteo_past;
  testTask = nullptr;
  update(true, true, now);
  assert(testTask);
  testTask(nullptr);
  assert(!https_gate::busyFlag());
  testTask = nullptr;
  update(true, true, now + 1);
}

uint32_t drain(uint32_t now) {
  using namespace openmeteo_past;
  unsigned remaining = 100;
  while (!days.empty() && remaining--) {
    if (completion) {
      const auto callback = completion;
      completion = nullptr;
      callback();
    }
    update(true, true, ++now);
  }
  assert(days.empty() && !completion);
  return now;
}
}

struct tm* testGmtime(const time_t* stamp, struct tm* output) {
  const struct tm* result = gmtime(stamp);
  if (!result) return nullptr;
  *output = *result;
  return output;
}

time_t testTime(time_t*) { return wallClock; }
time_t testMktime(struct tm* local) {
  char date[16];
  strftime(date, sizeof(date), "%Y-%m-%d", local);
  // Deterministic US Central timezone for the 2026 DST-boundary fixtures.
  const bool dst = (local->tm_mon > 2 && local->tm_mon < 10) ||
      (local->tm_mon == 2 && local->tm_mday >= 8);
  return static_cast<time_t>(openmeteo_past::dateSerial(date)) * 86400 +
      local->tm_hour * 3600 + (dst ? 5 : 6) * 3600;
}
struct tm* testLocaltime(const time_t* stamp, struct tm* output) {
  const bool dst = *stamp >= 1772956800 && *stamp < 1793516400;
  const time_t adjusted = *stamp - (dst ? 5 : 6) * 3600;
  return testGmtime(&adjusted, output);
}

namespace aio_publisher {
bool idle() { return idlePublisher; }
bool enqueueHistorical(const char* key, const char* value, const char* createdAt,
                       HistoryAcknowledged callback) {
  assert(!completion);
  if (!acceptGroup) return false;
  sent.push_back({key, value, createdAt});
  completion = callback;
  return true;
}
bool enqueueGroup(const char* group, const FeedSample* samples, size_t count) {
  assert(std::string(group) == "Open-Meteo past summaries" && count == 7);
  ++summaryAttempts;
  if (!acceptGroup) return false;
  for (size_t i = 0; i < count; ++i) {
    summaries[samples[i].key] = samples[i].value;
  }
  return true;
}
}

int main(int argc, char** argv) {
  using namespace openmeteo_past;
  assert(dateSerial("2024-02-29") >= 0);
  assert(dateSerial("2026-02-29") == -1);
  assert(dateSerial("2026-13-01") == -1);
  assert(dateSerial("2026-11-1") == -1);
  std::string stamp;
  assert(noonUtc("2026-10-31", stamp) && stamp == "2026-10-31T17:00:00Z");
  assert(noonUtc("2026-11-01", stamp) && stamp == "2026-11-01T18:00:00Z");
  assert(noonUtc("2026-03-07", stamp) && stamp == "2026-03-07T18:00:00Z");
  assert(noonUtc("2026-03-08", stamp) && stamp == "2026-03-08T17:00:00Z");
  reset();
  update(false, true, 0);
  update(true, false, 0);
  assert(!testTask);
  idlePublisher = false;
  update(true, true, 0);
  assert(!testTask);
  idlePublisher = true;
  assert(https_gate::tryAcquire("other"));
  update(true, true, 0);
  assert(!testTask && !attempted);
  https_gate::release();
  poll(0);
  assert(testUrl.find("&past_days=7&forecast_days=0&daily=") != std::string::npos);
  assert(testUrl.find("current=") == std::string::npos);
  assert(testUrl.find("timezone=America/Chicago") != std::string::npos);
  assert(sent.size() == 1 && fake_preferences::writes == 0);
  assert(sent[0].key == "omp-totprec" && sent[0].value == "0");
  assert(sent[0].createdAt == "2026-10-26T17:00:00Z");
  update(true, true, 2);
  assert(sent.size() == 1);  // Admission alone is not acknowledgement.
  drain(3);
  assert(sent.size() == 56 && fake_preferences::writes == 56);
  assert(summaries.size() == 7 && !summaries.count("om-past"));
  assert(summaries.at("ompast.0").find("For 2026-11-01") == 0);
  assert(summaries.at("ompast.6").find("For 2026-10-26") == 0);
  assert(summaries.at("ompast.0").find("daylight 11.1 hours") != std::string::npos);
  assert(summaries.at("ompast.0").find("sunshine for 5.5 hours (50%)") != std::string::npos);
  assert(sent[48].createdAt == "2026-11-01T18:00:00Z");
  testTask = nullptr;
  update(true, true, kPollMs - 1);
  assert(!testTask);
  poll(kPollMs);
  drain(kPollMs + 1);
  assert(sent.size() == 56 && skippedCount == 56);

  reset(true);
  poll(0);
  drain(1);
  assert(sent.empty() && fake_preferences::writes == 0 && skippedCount == 56);
  JsonDocument changed;
  assert(!deserializeJson(changed, testHttpBody));
  changed["daily"]["temperature_2m_max"][6] = 71;
  testHttpBody.clear();
  serializeJson(changed, testHttpBody);
  poll(kPollMs);
  drain(kPollMs + 1);
  assert(sent.size() == 1 && sent[0].key == "omp-maxtemp" && sent[0].value == "71");
  assert(fake_preferences::writes == 1);
  changed["daily"]["time"][0] = "2026-10-27";
  changed["daily"]["time"][1] = "2026-10-28";
  changed["daily"]["time"][2] = "2026-10-29";
  changed["daily"]["time"][3] = "2026-10-30";
  changed["daily"]["time"][4] = "2026-10-31";
  changed["daily"]["time"][5] = "2026-11-01";
  changed["daily"]["time"][6] = "2026-11-02";
  changed["daily"]["temperature_2m_max"][5] = 71;
  changed["daily"]["temperature_2m_max"][6] = 70;
  testHttpBody.clear();
  serializeJson(changed, testHttpBody);
  wallClock += 86400;
  poll(2 * kPollMs);
  drain(2 * kPollMs + 1);
  assert(sent.size() == 9 && skippedCount == 48);
  assert(sent.back().createdAt == "2026-11-02T18:00:00Z");
  assert(summaries.at("ompast.0").find("For 2026-11-02") == 0);

  // An interrupted unacknowledged sample is replayed after a restart.
  reset();
  poll(0);
  assert(!fake_preferences::writes && sent.size() == 1);
  reset(true);
  poll(0);
  assert(sent.size() == 1 && sent[0].key == "omp-totprec");

  // Store failure holds later samples, retries persistence, never resends the ACKed point.
  fake_preferences::failWrite = true;
  completion();
  completion = nullptr;
  update(true, true, 2);
  assert(sent.size() == 1 && persistenceDirty);
  fake_preferences::failWrite = false;
  update(true, true, 2 + kRetryMs - 1);
  assert(sent.size() == 1);
  update(true, true, 2 + kRetryMs);
  assert(sent.size() == 2 && fake_preferences::writes == 1);

  reset();
  fake_preferences::failOpen = true;
  update(true, true, 0);
  assert(!testTask && !persistenceReady);
  fake_preferences::failOpen = false;
  update(true, true, kRetryMs - 1);
  assert(!testTask);
  update(true, true, kRetryMs);
  assert(testTask && persistenceReady);

  reset();
  acceptGroup = false;
  poll(0);
  assert(!completion && !sent.size());
  acceptGroup = true;
  update(true, true, kAdmissionRetryMs);
  assert(sent.empty());
  update(true, true, kAdmissionRetryMs + 1);
  assert(sent.size() == 1);

  reset();
  poll(0);
  for (size_t i = 0; i < 56; ++i) {
    assert(completion);
    const auto callback = completion;
    completion = nullptr;
    callback();
    if (i == 55) acceptGroup = false;
    update(true, true, static_cast<uint32_t>(i + 2));
  }
  assert(sent.size() == 56 && fake_preferences::writes == 56);
  assert(summaryAttempts == 1 && summaries.empty() && !days.empty());
  acceptGroup = true;
  update(true, true, 57 + kAdmissionRetryMs - 1);
  assert(summaryAttempts == 1);
  update(true, true, 57 + kAdmissionRetryMs);
  assert(summaryAttempts == 2 && summaries.size() == 7 && days.empty());
  assert(sent.size() == 56 && fake_preferences::writes == 56);

  reset();
  fake_preferences::storage()["day0"] = "corrupt";
  poll(0);
  drain(1);
  assert(sent.size() == 56);
  assert(Serial.output.find("invalid/unreadable cache record day0") != std::string::npos);

  reset();
  JsonDocument wet;
  assert(!deserializeJson(wet, testHttpBody));
  wet["daily"]["precipitation_sum"][0] = 0.6;
  wet["daily"]["rain_sum"][0] = 0.3;
  wet["daily"]["showers_sum"][0] = 0.1;
  wet["daily"]["snowfall_sum"][0] = 2;
  testHttpBody.clear();
  serializeJson(wet, testHttpBody);
  poll(0);
  drain(1);
  const char* expected[] = {"0.6", "0.3", "0.1", "2", "70", "50", "11.1", "5.5"};
  for (size_t i = 0; i < kScalars; ++i) {
    assert(sent[i].key == kKeys[i] && sent[i].value == expected[i]);
  }
  assert(summaries.at("ompast.6").find("Precipitation totalled 0.6") != std::string::npos);

  // Nulls, bad units, missing/extra dates, unknown conditions, and incomplete days reject the snapshot.
  struct Invalid { const char* section; const char* field; const char* value; };
  for (const Invalid& invalid : {
      Invalid{"daily_units", "snowfall_sum", "\"cm\""},
      Invalid{"daily_units", "temperature_2m_max", "\"C\""},
      Invalid{"daily", "temperature_2m_max", "[null,70,70,70,70,70,70]"},
      Invalid{"daily", "rain_sum", "[-1,0,0,0,0,0,0]"},
      Invalid{"daily", "weather_code", "[999,0,0,0,0,0,0]"},
      Invalid{"daily", "time", "[\"2026-10-27\",\"2026-10-27\",\"2026-10-28\",\"2026-10-29\",\"2026-10-30\",\"2026-10-31\",\"2026-11-01\"]"},
      Invalid{"daily", "time", "[]"},
      Invalid{"daily", "sunshine_duration", "[86400,20000,20000,20000,20000,20000,20000]"}}) {
    reset();
    JsonDocument document, replacement;
    assert(!deserializeJson(document, testHttpBody));
    assert(!deserializeJson(replacement, invalid.value));
    document[invalid.section][invalid.field] = replacement.as<JsonVariantConst>();
    testHttpBody.clear();
    serializeJson(document, testHttpBody);
    poll(0);
    assert(days.empty() && sent.empty() && nextAttemptDelay == kRetryMs);
    assert(Serial.output.find("ERROR:") != std::string::npos);
  }
  for (int status : {0, -1, 429, 500}) {
    reset();
    testHttpStatus = status;
    poll(0);
    assert(sent.empty() && nextAttemptDelay == kRetryMs);
  }
  reset();
  testHttpReadFailure = true;
  poll(0);
  assert(sent.empty());
  reset();
  testHttpBody.assign(kMaxResponseBytes + 1, ' ');
  poll(0);
  assert(sent.empty());
  reset();
  testTaskFailure = true;
  update(true, true, 0);
  assert(!workerRunning && !https_gate::busyFlag() && nextAttemptDelay == kRetryMs);
  reset();
  const uint32_t start = UINT32_MAX - 1000;
  testHttpStatus = 500;
  poll(start);
  testTask = nullptr;
  update(true, true, start + kRetryMs);
  assert(!testTask);
  update(true, true, start + kRetryMs + 1);
  assert(testTask);

  if (argc > 1) {
    std::ifstream input(argv[1]);
    assert(input);
    const std::string body((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    std::vector<Day> parsed;
    assert(parseResponse(body, dateSerial("2026-11-02"), parsed));
    assert(parsed.size() == kDays);
  }
  puts("Open-Meteo past tests passed.");
}
