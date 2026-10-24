#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <mqtt_client.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "diagnostic_summary.h"

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

#ifndef AIO_WEATHER_LOCATION_ID
#define AIO_WEATHER_LOCATION_ID ""
#endif

namespace {
constexpr uint32_t kStartupWaitMs = 10000;
constexpr uint32_t kWifiAttemptMs = 20000;
constexpr uint32_t kTimeWaitLogMs = 15000;
constexpr uint32_t kSummaryIntervalMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kConfigReminderMs = 30000;
constexpr uint32_t kDiagPublishIntervalMs = 2000;
constexpr size_t kMaxStoredPayload = 16384;
constexpr size_t kSerialTxBuffer = 16384;
constexpr size_t kMaxPendingLogBytes = 65536;
constexpr size_t kMaxDiffLines = 15;
constexpr size_t kSummaryTopFields = 10;
constexpr int kMqttBufferSize = 4096;
constexpr int kMqttTaskStack = 8192;
constexpr time_t kValidEpoch = 1700000000;

constexpr int kNoTopic = -1;
constexpr int kErrorsTopic = -2;
constexpr int kThrottleTopic = -3;
constexpr int kUnknownTopic = -4;

const char kBrokerUri[] = "mqtts://io.adafruit.com:8883";

// DigiCert Global Root G2 (valid until 2038-01-15) anchors the io.adafruit.com
// chain. Pinning the root survives renewals of the server certificate.
const char kRootCa[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)PEM";

const char* const kForecastTypes[] = {
    "current",          "forecast_minutes_5", "forecast_minutes_30",
    "forecast_hours_1", "forecast_hours_2",   "forecast_hours_6",
    "forecast_hours_24", "forecast_days_1",   "forecast_days_2",
    "forecast_days_5",
};
constexpr size_t kTopicCount = sizeof(kForecastTypes) / sizeof(kForecastTypes[0]);

struct TopicStats {
  char topic[128] = {};
  uint32_t subscribedAt = 0;
  bool awaitingFirst = false;
  uint32_t count = 0;
  uint32_t changes = 0;
  uint32_t retained = 0;
  uint32_t truncated = 0;
  size_t lastSize = 0;
  size_t maxSize = 0;
  uint32_t lastRxAt = 0;
  uint64_t intervalSumMs = 0;
  uint32_t intervalCount = 0;
  uint32_t lastHash = 0;
  bool lastTruncated = false;
  std::string payload;
  std::map<std::string, uint32_t> fieldChanges;
};

struct Assembly {
  int index = kNoTopic;
  std::string topic;
  size_t total = 0;
  bool retain = false;
  uint32_t hash = 0;
  bool truncated = false;
  std::string data;
};

TopicStats topics[kTopicCount];
char errorsTopic[96] = {};
char throttleTopic[96] = {};
std::string diagnosticTopic;
std::string pendingSummary;
uint32_t lastDiagPublishAt = 0;
Assembly assembly;

// Guards statistics shared between the MQTT task and loop(). Never call
// esp_mqtt_client_* while holding it: the MQTT task holds the client's API lock
// while dispatching events, so that would deadlock.
std::mutex statsLock;

// The MQTT task queues its output and loop() writes it. Arduino-ESP32 2.0.x's
// USB CDC driver drops bytes when written from a core other than the one running
// its interrupt (loop()'s core), because a write can commit the hardware FIFO
// while the interrupt is still filling it.
std::mutex queueLock;
std::deque<std::string> pendingLog;
size_t pendingBytes = 0;
uint32_t droppedLogLines = 0;

esp_mqtt_client_handle_t client = nullptr;
std::atomic<bool> mqttConnected{false};
uint32_t mqttConnects = 0;
uint32_t mqttDisconnects = 0;
uint32_t mqttErrors = 0;
bool verbose = false;

bool configured = false;
size_t wifiIndex = 0;
bool wifiAttempting = false;
uint32_t wifiAttemptAt = 0;
bool wifiWasConnected = false;
bool timeConfigured = false;
bool timeValid = false;
std::atomic<bool> sntpSynced{false};
uint32_t timeWaitLoggedAt = 0;
uint32_t lastSummaryAt = 0;
uint32_t lastReminderAt = 0;

void queueLog(std::string line) {
  std::lock_guard<std::mutex> guard(queueLock);
  if (pendingBytes + line.size() > kMaxPendingLogBytes) {
    ++droppedLogLines;
    return;
  }
  pendingBytes += line.size();
  pendingLog.push_back(std::move(line));
}

void queueLogf(const char* format, ...) __attribute__((format(printf, 1, 2)));
void queueLogf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  const int length = vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  if (length < 0) {
    va_end(args);
    return;
  }
  std::vector<char> buffer(static_cast<size_t>(length) + 1);
  vsnprintf(buffer.data(), buffer.size(), format, args);
  va_end(args);
  queueLog(std::string(buffer.data(), static_cast<size_t>(length)));
}

// Called only from loop(), on the same core as the USB CDC interrupt.
void drainLog() {
  std::deque<std::string> lines;
  uint32_t dropped = 0;
  {
    std::lock_guard<std::mutex> guard(queueLock);
    lines.swap(pendingLog);
    pendingBytes = 0;
    dropped = droppedLogLines;
    droppedLogLines = 0;
  }
  for (const std::string& line : lines) {
    Serial.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
  }
  if (dropped > 0) {
    Serial.printf("(%lu log lines dropped: output queue full)\n",
                  static_cast<unsigned long>(dropped));
  }
}

uint32_t fnv1a(uint32_t hash, const char* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= static_cast<uint8_t>(data[i]);
    hash *= 16777619UL;
  }
  return hash;
}

using FlatJson = std::map<std::string, std::string>;

void flattenJson(JsonVariantConst value, const std::string& path, FlatJson& out) {
  if (value.is<JsonObjectConst>()) {
    for (JsonPairConst pair : value.as<JsonObjectConst>()) {
      flattenJson(pair.value(), path.empty() ? pair.key().c_str()
                                             : path + "." + pair.key().c_str(),
                  out);
    }
  } else if (value.is<JsonArrayConst>()) {
    size_t index = 0;
    for (JsonVariantConst item : value.as<JsonArrayConst>()) {
      flattenJson(item, path + "[" + std::to_string(index++) + "]", out);
    }
  } else {
    std::string text;
    serializeJson(value, text);
    out[path] = text;
  }
}

bool parseFlat(const std::string& payload, FlatJson& out) {
  JsonDocument document;
  if (deserializeJson(document, payload)) {
    return false;
  }
  flattenJson(document.as<JsonVariantConst>(), "", out);
  return true;
}

// Returns field-level differences ("path: old -> new") and counts them per field.
std::vector<std::string> diffPayloads(const std::string& before, const std::string& after,
                                      std::map<std::string, uint32_t>& fieldChanges) {
  std::vector<std::string> lines;
  FlatJson previous;
  FlatJson current;
  if (!parseFlat(before, previous) || !parseFlat(after, current)) {
    lines.push_back("(field diff unavailable: JSON parse failed)");
    return lines;
  }
  for (const auto& entry : current) {
    const auto old = previous.find(entry.first);
    if (old == previous.end()) {
      ++fieldChanges[entry.first];
      lines.push_back(entry.first + ": (added) -> " + entry.second);
    } else if (old->second != entry.second) {
      ++fieldChanges[entry.first];
      lines.push_back(entry.first + ": " + old->second + " -> " + entry.second);
    }
  }
  for (const auto& entry : previous) {
    if (current.find(entry.first) == current.end()) {
      ++fieldChanges[entry.first];
      lines.push_back(entry.first + ": " + entry.second + " -> (removed)");
    }
  }
  return lines;
}

void onTimeSync(struct timeval*) { sntpSynced = true; }

void formatDuration(uint64_t ms, char* out, size_t size) {
  const uint64_t totalSeconds = ms / 1000;
  const unsigned hours = static_cast<unsigned>(totalSeconds / 3600);
  const unsigned minutes = static_cast<unsigned>((totalSeconds / 60) % 60);
  const unsigned seconds = static_cast<unsigned>(totalSeconds % 60);
  if (hours > 0) {
    snprintf(out, size, "%uh%02um%02us", hours, minutes, seconds);
  } else if (minutes > 0) {
    snprintf(out, size, "%um%02us", minutes, seconds);
  } else {
    snprintf(out, size, "%u.%01us", seconds, static_cast<unsigned>((ms % 1000) / 100));
  }
}

void formatStamp(char* out, size_t size) {
  char clock[32] = "time not set";
  const time_t now = time(nullptr);
  if (now >= kValidEpoch) {
    struct tm local;
    localtime_r(&now, &local);
    strftime(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S %Z", &local);
  }
  char uptime[24];
  formatDuration(static_cast<uint64_t>(esp_timer_get_time() / 1000), uptime, sizeof(uptime));
  snprintf(out, size, "[%s | up %s]", clock, uptime);
}

void printHelp() {
  Serial.println("\n=== WEATHERKIT MQTT DIAGNOSTIC ===");
  Serial.printf("Location ID: %s; broker: %s (TLS verified)\n",
                AIO_WEATHER_LOCATION_ID, kBrokerUri);
  Serial.println("Subscribes to all 10 WeatherKit Power-Up topics and logs every message.");
  Serial.println("Commands: s = summary, p = print latest payloads, v = toggle full payload");
  Serial.println("logging, r = resubscribe (tests initial delivery), ? = help.");
  Serial.println("Summaries also go to aggregator.diag (at most 1024 bytes).");
  Serial.println("The Adafruit IO key and Wi-Fi passwords are never logged.");
}

void printSummary() {
  char stamp[80];
  formatStamp(stamp, sizeof(stamp));
  Serial.printf("\n=== SUMMARY %s ===\n", stamp);
  Serial.printf("MQTT connected=%s connects=%lu disconnects=%lu errors=%lu | "
                "Wi-Fi %s RSSI=%ld dBm | heap free=%lu min=%lu\n",
                mqttConnected.load() ? "yes" : "no",
                static_cast<unsigned long>(mqttConnects),
                static_cast<unsigned long>(mqttDisconnects),
                static_cast<unsigned long>(mqttErrors),
                WiFi.status() == WL_CONNECTED ? "up" : "down",
                static_cast<long>(WiFi.RSSI()),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()));
  Serial.println("topic                msgs  changed  retained  last B   max B  "
                 "avg interval  last rx ago");
  const uint32_t now = millis();
  for (const TopicStats& stats : topics) {
    const char* type = strrchr(stats.topic, '/');
    type = type ? type + 1 : stats.topic;
    char average[24] = "-";
    char ago[24] = "never";
    if (stats.intervalCount > 0) {
      formatDuration(stats.intervalSumMs / stats.intervalCount, average, sizeof(average));
    }
    if (stats.count > 0) {
      formatDuration(now - stats.lastRxAt, ago, sizeof(ago));
    }
    Serial.printf("%-20s %5lu  %7lu  %8lu  %6u  %6u  %12s  %11s%s\n", type,
                  static_cast<unsigned long>(stats.count),
                  static_cast<unsigned long>(stats.changes),
                  static_cast<unsigned long>(stats.retained),
                  static_cast<unsigned>(stats.lastSize),
                  static_cast<unsigned>(stats.maxSize), average, ago,
                  stats.truncated ? "  (stored copy truncated)" : "");
  }
  Serial.println("Most frequently changed fields (change count):");
  for (const TopicStats& stats : topics) {
    if (stats.fieldChanges.empty()) {
      continue;
    }
    std::vector<std::pair<std::string, uint32_t>> ranked(stats.fieldChanges.begin(),
                                                         stats.fieldChanges.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<std::string, uint32_t>& a,
                 const std::pair<std::string, uint32_t>& b) { return a.second > b.second; });
    const char* type = strrchr(stats.topic, '/');
    Serial.printf("  %s:", type ? type + 1 : stats.topic);
    for (size_t i = 0; i < ranked.size() && i < kSummaryTopFields; ++i) {
      Serial.printf(" %s=%lu", ranked[i].first.c_str(),
                    static_cast<unsigned long>(ranked[i].second));
    }
    if (ranked.size() > kSummaryTopFields) {
      Serial.printf(" (+%u more fields)", static_cast<unsigned>(ranked.size() - kSummaryTopFields));
    }
    Serial.println();
  }

  // Called with statsLock held; defer MQTT API calls to loop() to avoid lock inversion.
  diagnostic_summary::Topic snapshot[kTopicCount];
  const char* const names[] = {
      "cur", "min5", "min30", "hr1", "hr2", "hr6", "hr24", "day1", "day2", "day5"};
  static_assert(sizeof(names) / sizeof(names[0]) == kTopicCount,
                "Each WeatherKit topic needs a diagnostic label");
  for (size_t i = 0; i < kTopicCount; ++i) {
    const TopicStats& stats = topics[i];
    snapshot[i] = {
        names[i], stats.count, stats.changes, stats.retained,
        static_cast<uint32_t>(stats.maxSize),
        stats.intervalCount
            ? static_cast<uint32_t>(stats.intervalSumMs / stats.intervalCount / 1000)
            : 0,
        (now - stats.lastRxAt) / 1000};
  }
  const diagnostic_summary::Status status = {
      stamp, mqttConnected.load(), mqttConnects, mqttDisconnects, mqttErrors,
      WiFi.RSSI(), ESP.getFreeHeap(), ESP.getMinFreeHeap()};
  char summary[diagnostic_summary::kMaxBytes + 1];
  if (!diagnostic_summary::format(summary, status, snapshot, kTopicCount)) {
    Serial.println("ERROR: diagnostic summary exceeds 1024 bytes; not published.");
    return;
  }
  if (!pendingSummary.empty()) {
    Serial.println("Diagnostic feed: replacing unsent summary with latest snapshot.");
  }
  pendingSummary = summary;
}

void publishSummary(uint32_t now) {
  if (pendingSummary.empty() || !mqttConnected.load() ||
      now - lastDiagPublishAt < kDiagPublishIntervalMs) {
    return;
  }
  lastDiagPublishAt = now;
  const int id = esp_mqtt_client_enqueue(
      client, diagnosticTopic.c_str(), pendingSummary.c_str(),
      static_cast<int>(pendingSummary.size()), 1, 0, true);
  if (id < 0) {
    Serial.println("ERROR: aggregator.diag enqueue failed; will retry.");
    return;
  }
  Serial.printf("Diagnostic feed: queued %u bytes to aggregator.diag (msg_id=%d).\n",
                static_cast<unsigned>(pendingSummary.size()), id);
  pendingSummary.clear();
}

void printPayloads() {
  for (const TopicStats& stats : topics) {
    Serial.printf("\n--- %s (%u bytes) ---\n", stats.topic,
                  static_cast<unsigned>(stats.lastSize));
    if (stats.count == 0) {
      Serial.println("(no message yet)");
    } else {
      Serial.write(reinterpret_cast<const uint8_t*>(stats.payload.data()),
                   stats.payload.size());
      Serial.println();
    }
  }
}

// Called from the MQTT task with statsLock held.
void subscribeAll(esp_mqtt_client_handle_t mqtt) {
  char stamp[80];
  formatStamp(stamp, sizeof(stamp));
  const uint32_t now = millis();
  for (TopicStats& stats : topics) {
    stats.subscribedAt = now;
    stats.awaitingFirst = true;
    const int msgId = esp_mqtt_client_subscribe(mqtt, stats.topic, 0);
    queueLogf("%s SUBSCRIBE %s msg_id=%d\n", stamp, stats.topic, msgId);
  }
  esp_mqtt_client_subscribe(mqtt, errorsTopic, 0);
  esp_mqtt_client_subscribe(mqtt, throttleTopic, 0);
}

void logMqttError(const char* stamp, const esp_mqtt_error_codes_t* error) {
  if (!error) {
    queueLogf("%s MQTT ERROR: no details.\n", stamp);
    return;
  }
  if (error->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
    queueLogf("%s MQTT TRANSPORT ERROR: esp_err=%s tls_stack=0x%x cert_flags=0x%x "
              "errno=%d\n",
              stamp, esp_err_to_name(error->esp_tls_last_esp_err),
              static_cast<unsigned>(error->esp_tls_stack_err),
              static_cast<unsigned>(error->esp_tls_cert_verify_flags),
              error->esp_transport_sock_errno);
  } else if (error->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
    const char* reason = "unknown";
    switch (error->connect_return_code) {
      case MQTT_CONNECTION_REFUSE_PROTOCOL: reason = "protocol"; break;
      case MQTT_CONNECTION_REFUSE_ID_REJECTED: reason = "client ID rejected"; break;
      case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE: reason = "server unavailable"; break;
      case MQTT_CONNECTION_REFUSE_BAD_USERNAME: reason = "bad username"; break;
      case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
        reason = "not authorized (check AIO_USERNAME/AIO_KEY)";
        break;
      default: break;
    }
    queueLogf("%s MQTT CONNECTION REFUSED: %d (%s)\n", stamp,
              static_cast<int>(error->connect_return_code), reason);
  } else {
    queueLogf("%s MQTT ERROR: type=%d\n", stamp, static_cast<int>(error->error_type));
  }
}

int identifyTopic(const std::string& topic) {
  for (size_t i = 0; i < kTopicCount; ++i) {
    if (topic == topics[i].topic) {
      return static_cast<int>(i);
    }
  }
  if (topic == errorsTopic) {
    return kErrorsTopic;
  }
  if (topic == throttleTopic) {
    return kThrottleTopic;
  }
  return kUnknownTopic;
}

void completeMessage() {
  char stamp[80];
  formatStamp(stamp, sizeof(stamp));
  const uint32_t now = millis();
  if (assembly.index == kErrorsTopic || assembly.index == kThrottleTopic) {
    queueLogf("%s ADAFRUIT IO %s: %s\n", stamp,
              assembly.index == kErrorsTopic ? "ERROR" : "THROTTLE",
              assembly.data.c_str());
    return;
  }
  if (assembly.index == kUnknownTopic) {
    queueLogf("%s UNEXPECTED TOPIC %s (%u bytes)\n", stamp, assembly.topic.c_str(),
              static_cast<unsigned>(assembly.total));
    return;
  }

  TopicStats& stats = topics[assembly.index];
  const bool first = stats.count == 0;
  const bool changed = first || assembly.hash != stats.lastHash;
  std::vector<std::string> diff;
  if (!first && changed) {
    if (assembly.truncated || stats.lastTruncated) {
      diff.push_back("(field diff unavailable: payload exceeded stored size)");
    } else {
      diff = diffPayloads(stats.payload, assembly.data, stats.fieldChanges);
    }
  }
  char interval[24] = "-";
  if (!first) {
    const uint32_t elapsed = now - stats.lastRxAt;
    formatDuration(elapsed, interval, sizeof(interval));
    stats.intervalSumMs += elapsed;
    ++stats.intervalCount;
    if (changed) {
      ++stats.changes;
    }
  }
  char latency[48] = "";
  if (stats.awaitingFirst) {
    char wait[24];
    formatDuration(now - stats.subscribedAt, wait, sizeof(wait));
    snprintf(latency, sizeof(latency), ", %s after subscribe", wait);
    stats.awaitingFirst = false;
  }
  ++stats.count;
  if (assembly.retain) {
    ++stats.retained;
  }
  if (assembly.truncated) {
    ++stats.truncated;
  }
  stats.lastSize = assembly.total;
  if (assembly.total > stats.maxSize) {
    stats.maxSize = assembly.total;
  }
  stats.lastRxAt = now;
  stats.lastHash = assembly.hash;
  stats.lastTruncated = assembly.truncated;
  stats.payload.swap(assembly.data);

  const char* type = kForecastTypes[assembly.index];
  queueLogf("%s RX %s: %u bytes, %s, retain=%d, since previous %s%s, heap=%lu\n", stamp,
            type, static_cast<unsigned>(assembly.total),
            first ? "first" : (changed ? "CHANGED" : "unchanged"), assembly.retain ? 1 : 0,
            interval, latency, static_cast<unsigned long>(ESP.getFreeHeap()));
  for (size_t i = 0; i < diff.size() && i < kMaxDiffLines; ++i) {
    queueLogf("    %s\n", diff[i].c_str());
  }
  if (diff.size() > kMaxDiffLines) {
    queueLogf("    ... %u more changed fields\n",
              static_cast<unsigned>(diff.size() - kMaxDiffLines));
  }
  if (first || verbose) {
    queueLog(stats.payload + (assembly.truncated ? "\n(payload truncated in this log)\n"
                                                 : "\n"));
  }
}

// esp-mqtt splits messages larger than its buffer into several DATA events;
// only the first carries the topic.
void handleData(const esp_mqtt_event_t* event) {
  if (event->current_data_offset == 0) {
    assembly.topic.assign(event->topic ? event->topic : "",
                          event->topic ? static_cast<size_t>(event->topic_len) : 0);
    assembly.index = identifyTopic(assembly.topic);
    assembly.total = static_cast<size_t>(event->total_data_len);
    assembly.retain = event->retain;
    assembly.hash = 2166136261UL;
    assembly.truncated = false;
    assembly.data.clear();
    assembly.data.reserve(assembly.total < kMaxStoredPayload ? assembly.total
                                                              : kMaxStoredPayload);
  }
  if (assembly.index == kNoTopic || !event->data || event->data_len <= 0) {
    return;
  }
  const size_t length = static_cast<size_t>(event->data_len);
  assembly.hash = fnv1a(assembly.hash, event->data, length);
  const size_t room = kMaxStoredPayload - assembly.data.size();
  if (length > room) {
    assembly.truncated = true;
  }
  assembly.data.append(event->data, length < room ? length : room);
  if (event->current_data_offset + event->data_len >= event->total_data_len) {
    completeMessage();
    assembly.index = kNoTopic;
  }
}

void onMqttEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  const esp_mqtt_event_t* event = static_cast<esp_mqtt_event_handle_t>(eventData);
  std::lock_guard<std::mutex> guard(statsLock);
  char stamp[80];
  switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
    case MQTT_EVENT_BEFORE_CONNECT:
      formatStamp(stamp, sizeof(stamp));
      queueLogf("%s MQTT connecting...\n", stamp);
      break;
    case MQTT_EVENT_CONNECTED:
      mqttConnected = true;
      ++mqttConnects;
      formatStamp(stamp, sizeof(stamp));
      queueLogf("%s MQTT CONNECTED (session_present=%d)\n", stamp,
                event->session_present);
      subscribeAll(event->client);
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqttConnected = false;
      ++mqttDisconnects;
      assembly.index = kNoTopic;
      formatStamp(stamp, sizeof(stamp));
      queueLogf("%s MQTT DISCONNECTED; the client retries automatically.\n", stamp);
      break;
    case MQTT_EVENT_SUBSCRIBED:
      if (verbose) {
        queueLogf("SUBACK msg_id=%d\n", event->msg_id);
      }
      break;
    case MQTT_EVENT_PUBLISHED:
      queueLogf("Diagnostic feed: MQTT acknowledged summary msg_id=%d.\n",
                event->msg_id);
      break;
    case MQTT_EVENT_ERROR:
      ++mqttErrors;
      formatStamp(stamp, sizeof(stamp));
      logMqttError(stamp, event->error_handle);
      break;
    case MQTT_EVENT_DATA:
      handleData(event);
      break;
    default:
      break;
  }
}

void startMqtt() {
  esp_mqtt_client_config_t config = {};
  config.uri = kBrokerUri;
  config.username = AIO_USERNAME;
  config.password = AIO_KEY;
  config.cert_pem = kRootCa;
  config.keepalive = 60;
  config.buffer_size = kMqttBufferSize;
  config.task_stack = kMqttTaskStack;
  config.reconnect_timeout_ms = 15000;
  client = esp_mqtt_client_init(&config);
  if (!client) {
    Serial.println("ERROR: esp_mqtt_client_init failed.");
    return;
  }
  esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, onMqttEvent, nullptr);
  const esp_err_t started = esp_mqtt_client_start(client);
  Serial.printf("MQTT client start: %s\n", esp_err_to_name(started));
}

void resubscribe() {
  if (!client || !mqttConnected.load()) {
    Serial.println("MQTT is not connected; nothing to resubscribe.");
    return;
  }
  for (const TopicStats& stats : topics) {
    esp_mqtt_client_unsubscribe(client, stats.topic);
  }
  {
    std::lock_guard<std::mutex> guard(statsLock);
    char stamp[80];
    formatStamp(stamp, sizeof(stamp));
    Serial.printf("%s RESUBSCRIBE: unsubscribed all weather topics.\n", stamp);
    const uint32_t now = millis();
    for (TopicStats& stats : topics) {
      stats.subscribedAt = now;
      stats.awaitingFirst = true;
    }
  }
  for (const TopicStats& stats : topics) {
    esp_mqtt_client_subscribe(client, stats.topic, 0);
  }
}

bool hasWifiNetwork() {
  for (size_t i = 0; i < WIFI_NETWORK_COUNT; ++i) {
    if (wifiNetworks[i].ssid && wifiNetworks[i].ssid[0]) {
      return true;
    }
  }
  return false;
}

void beginNextWifi() {
  for (size_t tries = 0; tries < WIFI_NETWORK_COUNT; ++tries) {
    const size_t index = wifiIndex;
    wifiIndex = (wifiIndex + 1) % WIFI_NETWORK_COUNT;
    const WiFiCredential& credential = wifiNetworks[index];
    if (!credential.ssid || !credential.ssid[0]) {
      continue;
    }
    WiFi.disconnect(false, false);
    Serial.printf("Wi-Fi: trying credential #%u (%s)\n", static_cast<unsigned>(index + 1),
                  credential.ssid);
    WiFi.begin(credential.ssid, credential.password ? credential.password : "");
    wifiAttempting = true;
    wifiAttemptAt = millis();
    return;
  }
}

void updateWifi(uint32_t now) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wifiWasConnected) {
    wifiAttempting = false;
    Serial.printf("Wi-Fi CONNECTED: SSID=%s IP=%s RSSI=%ld dBm channel=%ld\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                  static_cast<long>(WiFi.RSSI()), static_cast<long>(WiFi.channel()));
    if (!timeConfigured) {
      sntp_set_time_sync_notification_cb(onTimeSync);
      configTzTime(TZ_STRING, NTP_SERVER_1, NTP_SERVER_2);
      timeConfigured = true;
      timeWaitLoggedAt = now;
    }
  } else if (!connected && wifiWasConnected) {
    Serial.println("Wi-Fi connection lost; reconnecting.");
  }
  wifiWasConnected = connected;
  if (!connected && (!wifiAttempting || now - wifiAttemptAt >= kWifiAttemptMs)) {
    beginNextWifi();
  }
}

void updateTimeAndMqtt(uint32_t now) {
  if (!timeConfigured || client) {
    return;
  }
  if (!timeValid) {
    // The RTC keeps time across resets, so wait for an actual SNTP sync rather
    // than accepting a plausible-looking but stale clock.
    if (!sntpSynced.load()) {
      if (now - timeWaitLoggedAt >= kTimeWaitLogMs) {
        Serial.println("Waiting for NTP time (required for TLS certificate checks)...");
        timeWaitLoggedAt = now;
      }
      return;
    }
    timeValid = true;
    char stamp[80];
    formatStamp(stamp, sizeof(stamp));
    Serial.printf("%s NTP synchronized.\n", stamp);
  }
  if (WiFi.status() == WL_CONNECTED) {
    startMqtt();
  }
}

void handleCommands() {
  while (Serial.available()) {
    const int command = Serial.read();
    if (command == 'r' || command == 'R') {
      resubscribe();
      continue;
    }
    std::lock_guard<std::mutex> guard(statsLock);
    if (command == 's' || command == 'S') {
      printSummary();
    } else if (command == 'p' || command == 'P') {
      printPayloads();
    } else if (command == 'v' || command == 'V') {
      verbose = !verbose;
      Serial.printf("Full payload logging %s.\n", verbose ? "ON" : "OFF");
    } else if (command == '?') {
      printHelp();
    }
  }
}
}  // namespace

void setup() {
  // The default 256-byte USB CDC buffer drops data when several payloads are
  // logged at once.
  Serial.setTxBufferSize(kSerialTxBuffer);
  Serial.begin(115200);
  // Give Upload and Monitor time to reopen native USB, without requiring it.
  const uint32_t startedAt = millis();
  while (millis() - startedAt < kStartupWaitMs) {
    delay(10);
  }
  printHelp();

  for (size_t i = 0; i < kTopicCount; ++i) {
    snprintf(topics[i].topic, sizeof(topics[i].topic), "%s/integration/weather/%s/%s",
             AIO_USERNAME, AIO_WEATHER_LOCATION_ID, kForecastTypes[i]);
  }
  snprintf(errorsTopic, sizeof(errorsTopic), "%s/errors", AIO_USERNAME);
  snprintf(throttleTopic, sizeof(throttleTopic), "%s/throttle", AIO_USERNAME);
  diagnosticTopic = std::string(AIO_USERNAME) + "/feeds/aggregator.diag";

  configured = AIO_USERNAME[0] && AIO_KEY[0] && AIO_WEATHER_LOCATION_ID[0] &&
               hasWifiNetwork();
  if (!configured) {
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  lastSummaryAt = millis();
}

void loop() {
  const uint32_t now = millis();
  if (!configured) {
    if (now - lastReminderAt >= kConfigReminderMs || lastReminderAt == 0) {
      Serial.println("NOT CONFIGURED: set AIO_USERNAME, AIO_KEY, AIO_WEATHER_LOCATION_ID "
                     "and at least one Wi-Fi network in include/credentials.h.");
      lastReminderAt = now == 0 ? 1 : now;
    }
    delay(100);
    return;
  }
  drainLog();
  handleCommands();
  updateWifi(now);
  updateTimeAndMqtt(now);
  if (now - lastSummaryAt >= kSummaryIntervalMs) {
    lastSummaryAt = now;
    std::lock_guard<std::mutex> guard(statsLock);
    printSummary();
  }
  publishSummary(now);
  delay(10);
}
