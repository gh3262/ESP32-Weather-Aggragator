#include "openmeteo_past.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>
#include <cctype>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "aio_publisher.h"
#include "https_gate.h"
#include "letsencrypt_root_ca.h"
#include "openmeteo_common.h"

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

namespace openmeteo_past {
namespace {
using namespace openmeteo_common;
constexpr uint32_t kPollMs = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kRetryMs = 60000;
constexpr uint32_t kAdmissionRetryMs = 2000;
constexpr size_t kMaxResponseBytes = 8192;
constexpr size_t kDays = 7;
constexpr size_t kScalars = 8;
constexpr uint32_t kRecordVersion = 1;
const char* const kKeys[] = {
    "omp-totprec", "omp-rain", "omp-showers", "omp-snow",
    "omp-maxtemp", "omp-mintemp", "omp-daylighthours", "omp-sunshinehrs"};

struct Field {
  const char* name;
  const char* unit;
  double minimum;
  double maximum;
  bool integral;
};
const Field kFields[] = {
    {"precipitation_sum", "inch", 0, 1000, false},
    {"rain_sum", "inch", 0, 1000, false},
    {"showers_sum", "inch", 0, 1000, false},
    {"snowfall_sum", "inch", 0, 1000, false},
    {"temperature_2m_max", "\xC2\xB0" "F", -200, 200, false},
    {"temperature_2m_min", "\xC2\xB0" "F", -200, 200, false},
    {"daylight_duration", "s", 0, 86400, false},
    {"sunshine_duration", "s", 0, 86400, false},
    {"weather_code", "wmo code", 0, 999, true},
    {"apparent_temperature_max", "\xC2\xB0" "F", -200, 200, false},
    {"apparent_temperature_min", "\xC2\xB0" "F", -200, 200, false},
    {"wind_gusts_10m_max", "mp/h", 0, 1000, false},
    {"wind_direction_10m_dominant", "\xC2\xB0", 0, 360, false}};
constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

struct Day {
  int serial = 0;
  std::string date;
  std::string createdAt;
  std::string values[kScalars];
  std::string summary;
};
struct Record {
  uint32_t version = 0;
  int32_t serial = 0;
  uint64_t hashes[kScalars] = {};
};
Preferences preferences;
Record records[8];
bool persistenceReady = false;
bool persistenceDirty = false;
size_t dirtySlot = 0;
bool persistenceRetry = false;
uint32_t persistenceRetryAt = 0;
bool configured = false;
bool attempted = false;
uint32_t lastAttemptAt = 0;
uint32_t nextAttemptDelay = 0;
bool admissionRetry = false;
uint32_t admissionRetryAt = 0;
std::vector<Day> days;
size_t dayIndex = 0;
size_t scalarIndex = 0;
bool awaitingAcknowledgement = false;
uint64_t awaitingHash = 0;
size_t publishedCount = 0;
size_t skippedCount = 0;

struct FetchResult {
  int status = 0;
  bool failed = false;
  std::string body;
};
std::mutex resultLock;
FetchResult workerResult;
bool workerRunning = false;
bool resultPending = false;
int requestedToday = 0;
std::string requestUrl;

class Body : public Stream {
 public:
  char data[kMaxResponseBytes] = {};
  size_t length = 0;
  bool overflow = false;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t count) override {
    const size_t copied = count < sizeof(data) - length ? count : sizeof(data) - length;
    memcpy(data + length, bytes, copied);
    length += copied;
    overflow = overflow || copied != count;
    return count;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

int dateSerial(const char* date) {
  if (!date || strlen(date) != 10) return -1;
  for (size_t i = 0; i < 10; ++i) {
    if (i == 4 || i == 7) {
      if (date[i] != '-') return -1;
    } else if (date[i] < '0' || date[i] > '9') return -1;
  }
  const int year = atoi(date), month = atoi(date + 5), day = atoi(date + 8);
  const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const auto leap = [](int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); };
  if (year < 1970 || year > 2099 || month < 1 || month > 12 || day < 1 ||
      day > lengths[month - 1] + (month == 2 && leap(year) ? 1 : 0)) return -1;
  int result = day - 1;
  for (int y = 1970; y < year; ++y) result += 365 + leap(y);
  for (int m = 1; m < month; ++m) result += lengths[m - 1] + (m == 2 && leap(year) ? 1 : 0);
  return result;
}

bool noonUtc(const char* date, std::string& output) {
  if (dateSerial(date) < 0) return false;
  struct tm local = {};
  local.tm_year = atoi(date) - 1900;
  local.tm_mon = atoi(date + 5) - 1;
  local.tm_mday = atoi(date + 8);
  local.tm_hour = 12;
  local.tm_isdst = -1;
  const time_t stamp = mktime(&local);
  struct tm check = {}, utc = {};
  char roundtrip[24], formatted[24];
  if (stamp == static_cast<time_t>(-1) || !localtime_r(&stamp, &check) ||
      !gmtime_r(&stamp, &utc) ||
      strftime(roundtrip, sizeof(roundtrip), "%Y-%m-%d", &check) != 10 ||
      strcmp(roundtrip, date) != 0 || check.tm_hour != 12 ||
      strftime(formatted, sizeof(formatted), "%Y-%m-%dT%H:%M:%SZ", &utc) != 20) {
    return false;
  }
  output = formatted;
  return true;
}

uint64_t fingerprint(const Day& day, size_t field) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (const std::string& part : {
      std::string(LATITUDE), std::string(LONGITUDE), std::string(TIMEZONE),
      day.createdAt, std::string(kKeys[field]), day.values[field]}) {
    for (unsigned char ch : part) {
      hash = (hash ^ ch) * UINT64_C(1099511628211);
    }
    hash = (hash ^ 0) * UINT64_C(1099511628211);
  }
  return hash;
}

bool saveRecord(uint32_t now) {
  if (!persistenceDirty) return true;
  if (persistenceRetry && now - persistenceRetryAt < kRetryMs) return false;
  char key[8];
  snprintf(key, sizeof(key), "day%u", static_cast<unsigned>(dirtySlot));
  if (preferences.putBytes(key, &records[dirtySlot], sizeof(Record)) != sizeof(Record)) {
    Serial.println("Open-Meteo past ERROR: unable to persist acknowledged history; retry in 60 seconds");
    persistenceRetry = true;
    persistenceRetryAt = now;
    return false;
  }
  persistenceDirty = persistenceRetry = false;
  return true;
}

void acknowledged() {
  const Day& day = days[dayIndex];
  const size_t slot = static_cast<size_t>(day.serial) % 8;
  Record& record = records[slot];
  if (record.version != kRecordVersion || record.serial != day.serial) {
    record = Record{};
    record.version = kRecordVersion;
    record.serial = day.serial;
  }
  record.hashes[scalarIndex] = awaitingHash;
  dirtySlot = slot;
  persistenceDirty = true;
  awaitingAcknowledgement = false;
  ++publishedCount;
  ++scalarIndex;
}

bool parseResponse(const std::string& body, int today, std::vector<Day>& parsed) {
  JsonDocument document;
  if (deserializeJson(document, body, DeserializationOption::NestingLimit(8)) ||
      !unitIs(document["timezone"], TIMEZONE) ||
      !document["daily"].is<JsonObject>() ||
      !unitIs(document["daily_units"]["time"], "iso8601")) {
    Serial.println("Open-Meteo past ERROR: invalid JSON, timezone, or daily/time units");
    return false;
  }
  const JsonObjectConst daily = document["daily"].as<JsonObjectConst>();
  const JsonObjectConst units = document["daily_units"].as<JsonObjectConst>();
  if (!daily["time"].is<JsonArrayConst>() || daily["time"].size() != kDays) {
    Serial.println("Open-Meteo past ERROR: expected exactly seven daily dates");
    return false;
  }
  for (const Field& field : kFields) {
    if (!unitIs(units[field.name], field.unit) ||
        !daily[field.name].is<JsonArrayConst>() || daily[field.name].size() != kDays) {
      Serial.printf("Open-Meteo past ERROR: invalid array/units for %s\n", field.name);
      return false;
    }
  }
  for (size_t i = 0; i < kDays; ++i) {
    Day day;
    const char* date = daily["time"][i].as<const char*>();
    day.serial = dateSerial(date);
    if (day.serial != today - static_cast<int>(kDays - i) ||
        !noonUtc(date, day.createdAt)) {
      Serial.println("Open-Meteo past ERROR: unexpected daily date or invalid local-noon conversion");
      return false;
    }
    day.date = date;
    double values[kFieldCount] = {};
    for (size_t field = 0; field < kFieldCount; ++field) {
      const Field& spec = kFields[field];
      if (!numeric(daily[spec.name][i], values[field]) ||
          values[field] < spec.minimum || values[field] > spec.maximum ||
          (spec.integral && std::trunc(values[field]) != values[field])) {
        Serial.printf("Open-Meteo past ERROR: invalid %s for %s\n", spec.name, date);
        return false;
      }
    }
    if (values[4] < values[5] || values[9] < values[10] ||
        values[7] > values[6]) {
      Serial.printf("Open-Meteo past ERROR: inconsistent daily ranges for %s\n", date);
      return false;
    }
    for (size_t field = 0; field < kScalars; ++field) {
      day.values[field] = rawNumber(field < 6 ? values[field] :
                                   std::trunc(values[field] / 3600 * 10) / 10);
    }
    const char* description = conditionText(static_cast<unsigned>(values[8]));
    if (!description) {
      Serial.printf("Open-Meteo past ERROR: unknown weather code for %s\n", date);
      return false;
    }
    const std::string degree = "\xC2\xB0" "F";
    day.summary = "For " + day.date + " - " + description +
        " with high of " + day.values[4] + degree + " and low of " + day.values[5] +
        degree + " feeling like a high of " + rawNumber(values[9]) + degree +
        " and a low of " + rawNumber(values[10]) + degree + ". Total daylight " +
        day.values[6] + " hours with sunshine for " + day.values[7] + " hours (" +
        rawNumber(values[6] > 0 ? std::trunc(values[7] / values[6] * 100) : 0) + "%). ";
    if (values[0] == 0) {
      day.summary += "There was no precipitation recorded. ";
    } else {
      day.summary += "Precipitation totalled " + day.values[0] +
          " inches of which was " + day.values[1] + " inches rain, " +
          day.values[2] + " inches showers and " + day.values[3] + " inches snow. ";
    }
    day.summary += "Winds were primarily from the " + std::string(windText(values[12])) +
        " gusting to " + rawNumber(values[11]) + " mp/h.";
    if (day.summary.size() > 512) {
      Serial.printf("Open-Meteo past ERROR: daily summary exceeds 512 bytes for %s\n", date);
      return false;
    }
    parsed.push_back(std::move(day));
  }
  return true;
}

FetchResult fetchHistory() {
  FetchResult result;
  WiFiClientSecure transport;
  transport.setCACert(letsencrypt_root_ca::kTrustedRootCa);
  transport.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  if (http.begin(transport, requestUrl.c_str())) {
    result.status = http.GET();
    if (result.status == HTTP_CODE_OK) {
      Body* body = new (std::nothrow) Body();
      if (!body) {
        result.failed = true;
      } else {
        const int read = http.writeToStream(body);
        result.failed = read < 0 || body->overflow;
        if (!result.failed) result.body.assign(body->data, body->length);
        delete body;
      }
    }
    http.end();
  }
  return result;
}

void fetchTask(void*) {
  // vTaskDelete does not unwind C++ objects; finish TLS cleanup first.
  FetchResult result = fetchHistory();
  https_gate::release();
  {
    std::lock_guard<std::mutex> guard(resultLock);
    workerResult = std::move(result);
    workerRunning = false;
    resultPending = true;
  }
  vTaskDelete(nullptr);
}

void consumeResult(uint32_t now) {
  FetchResult result;
  {
    std::lock_guard<std::mutex> guard(resultLock);
    if (!resultPending) return;
    result = std::move(workerResult);
    resultPending = false;
  }
  std::vector<Day> parsed;
  if (result.status != HTTP_CODE_OK || result.failed ||
      !parseResponse(result.body, requestedToday, parsed)) {
    Serial.printf("Open-Meteo past ERROR: request/response invalid (HTTP=%d); retry in 60 seconds\n",
                  result.status);
    nextAttemptDelay = kRetryMs;
    lastAttemptAt = now;
    return;
  }
  days = std::move(parsed);
  dayIndex = scalarIndex = publishedCount = skippedCount = 0;
  nextAttemptDelay = kPollMs;
  Serial.println("Open-Meteo past: seven completed days ready for paced publication");
}

void publishNext(uint32_t now) {
  if (days.empty() || awaitingAcknowledgement || !saveRecord(now) ||
      (admissionRetry && now - admissionRetryAt < kAdmissionRetryMs)) return;
  while (dayIndex < days.size()) {
    const Day& day = days[dayIndex];
    if (scalarIndex < kScalars) {
      const Record& record = records[static_cast<size_t>(day.serial) % 8];
      const uint64_t hash = fingerprint(day, scalarIndex);
      if (record.version == kRecordVersion && record.serial == day.serial &&
          record.hashes[scalarIndex] == hash) {
        ++skippedCount;
        ++scalarIndex;
        continue;
      }
      if (aio_publisher::enqueueHistorical(kKeys[scalarIndex],
              day.values[scalarIndex].c_str(), day.createdAt.c_str(), acknowledged)) {
        awaitingHash = hash;
        awaitingAcknowledgement = true;
        admissionRetry = false;
      } else {
        admissionRetry = true;
        admissionRetryAt = now;
      }
      return;
    }
    ++dayIndex;
    scalarIndex = 0;
  }
  std::string keys[kDays];
  aio_publisher::FeedSample summaries[kDays];
  for (size_t i = 0; i < kDays; ++i) {
    keys[i] = "ompast." + std::to_string(kDays - i - 1);
    summaries[i] = {keys[i].c_str(), days[i].summary.c_str()};
  }
  if (!aio_publisher::enqueueGroup("Open-Meteo past summaries", summaries, kDays)) {
    admissionRetry = true;
    admissionRetryAt = now;
    return;
  }
  admissionRetry = false;
  Serial.printf("Open-Meteo past: batch admitted; scalar writes confirmed=%u unchanged skipped=%u\n",
                static_cast<unsigned>(publishedCount), static_cast<unsigned>(skippedCount));
  days.clear();
}

bool validTimezone() {
  if (!TIMEZONE[0]) return false;
  for (const char* ch = TIMEZONE; *ch; ++ch) {
    if (!isalnum(static_cast<unsigned char>(*ch)) &&
        *ch != '/' && *ch != '_' && *ch != '-' && *ch != '+') return false;
  }
  return true;
}
}

void begin() {
  configured = validCoordinate(LATITUDE, -90, 90) &&
      validCoordinate(LONGITUDE, -180, 180) && validTimezone();
  if (!configured) {
    Serial.println("Open-Meteo past: disabled; configure coordinates and IANA TIMEZONE");
    return;
  }
  Serial.println("Open-Meteo past: enabled; seven completed days every six hours");
}

void update(bool wifiConnected, bool timeSynchronized, uint32_t now) {
  if (!configured) return;
  if (!persistenceReady) {
    if (persistenceRetry && now - persistenceRetryAt < kRetryMs) return;
    if (!preferences.begin("omp-history", false)) {
      Serial.println("Open-Meteo past ERROR: unable to open history storage; retry in 60 seconds");
      persistenceRetry = true;
      persistenceRetryAt = now;
      return;
    }
    for (size_t i = 0; i < 8; ++i) {
      char key[8];
      snprintf(key, sizeof(key), "day%u", static_cast<unsigned>(i));
      const size_t bytes = preferences.getBytesLength(key);
      if (bytes == sizeof(Record) &&
          preferences.getBytes(key, &records[i], sizeof(Record)) == sizeof(Record) &&
          records[i].version == kRecordVersion) continue;
      records[i] = Record{};
      if (bytes != 0) Serial.printf("Open-Meteo past ERROR: invalid/unreadable cache record %s; will replay dates\n", key);
    }
    persistenceReady = true;
    persistenceRetry = false;
  }
  consumeResult(now);
  publishNext(now);
  if (!days.empty() || !wifiConnected || !timeSynchronized ||
      (attempted && now - lastAttemptAt < nextAttemptDelay) ||
      !aio_publisher::idle()) return;
  struct tm local = {};
  const time_t stamp = time(nullptr);
  char today[16];
  if (!localtime_r(&stamp, &local) ||
      strftime(today, sizeof(today), "%Y-%m-%d", &local) != 10 ||
      dateSerial(today) < 0) {
    Serial.println("Open-Meteo past ERROR: invalid local date; retry in 60 seconds");
    attempted = true;
    lastAttemptAt = now;
    nextAttemptDelay = kRetryMs;
    return;
  }
  {
    std::lock_guard<std::mutex> guard(resultLock);
    if (workerRunning || resultPending || !https_gate::tryAcquire("Open-Meteo past")) return;
    workerRunning = true;
  }
  requestedToday = dateSerial(today);
  std::string timezone;
  for (const char* ch = TIMEZONE; *ch; ++ch) {
    timezone += *ch == '+' ? "%2B" : std::string(1, *ch);
  }
  requestUrl = std::string("https://api.open-meteo.com/v1/forecast?latitude=") +
      LATITUDE + "&longitude=" + LONGITUDE + "&timezone=" + timezone +
      "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch"
      "&past_days=7&forecast_days=0&daily=";
  for (size_t i = 0; i < kFieldCount; ++i) {
    if (i) requestUrl += ',';
    requestUrl += kFields[i].name;
  }
  attempted = true;
  lastAttemptAt = now;
  nextAttemptDelay = kPollMs;
  if (xTaskCreate(fetchTask, "om_past", 12288, nullptr, 1, nullptr) != pdPASS) {
    https_gate::release();
    std::lock_guard<std::mutex> guard(resultLock);
    workerRunning = false;
    nextAttemptDelay = kRetryMs;
    Serial.println("Open-Meteo past ERROR: unable to start worker; retry in 60 seconds");
  }
}

}
