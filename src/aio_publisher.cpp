#include "aio_publisher.h"
#include "weatherkit_current.h"

#include <Arduino.h>
#include <mqtt_client.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <time.h>

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

#ifndef AIO_WEATHER_LOCATION_ID
#define AIO_WEATHER_LOCATION_ID ""
#endif

namespace aio_publisher {
namespace {
constexpr uint32_t kPublishIntervalMs = 2000;
constexpr uint32_t kPublishTimeoutMs = 30000;
constexpr uint32_t kMqttStartRetryMs = 15000;
constexpr uint32_t kRatePollMs = 60000;
constexpr uint32_t kRateRetryMs = 15000;
constexpr uint32_t kThrottlePauseMs = 60000;
constexpr size_t kMaxQueuedPublishes = 64;
constexpr size_t kMaxFeedKeyLength = 64;
constexpr size_t kMaxValueLength = 512;
constexpr size_t kMaxGroupNameLength = 64;
constexpr size_t kMaxLogEntries = 16;
constexpr size_t kMaxTrackedFeeds = 384;
constexpr char kLogFeed[] = "aggregator.log";
constexpr char kDiagnosticFeed[] = "aggregator.diag";
constexpr size_t kMaxDiagnosticBytes = 1024;
constexpr uint32_t kDiagnosticIntervalMs = 30 * 60 * 1000;
constexpr uint32_t kRecoveryDiagnosticCooldownMs = 60000;
constexpr char kBrokerUri[] = "mqtts://io.adafruit.com:8883";

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

struct Publish {
  std::string topic;
  std::string value;
  std::string groupName;
  bool isGroupEnd;
  bool isLog;
  bool failureLogged;
  size_t groupUpdateCount;
  bool isDiagnostic;
};

struct PublishedValue {
  bool acknowledged = false;
  std::string value;
};

esp_mqtt_client_handle_t client = nullptr;
std::deque<Publish> pending;
std::deque<std::string> pendingLogs;
std::map<std::string, PublishedValue> publishedValues;
std::atomic<bool> mqttConnected{false};
std::atomic<int> acknowledgedMessageId{0};
std::atomic<int> mqttError{0};
std::atomic<bool> brokerReportedError{false};
std::atomic<bool> brokerReportedThrottle{false};
uint32_t brokerThrottleEvents = 0;
char brokerThrottleText[192] = {};
char errorsTopic[96] = {};
char throttleTopic[96] = {};
struct WeatherSubscription {
  const char* type;
  weatherkit_current::Group group;
  std::string topic;
  std::atomic<bool> failed{false};
  bool retry = false;
  uint32_t lastRetryAt = 0;

  WeatherSubscription(const char* type, weatherkit_current::Group group)
      : type(type), group(group) {}
};
WeatherSubscription weatherSubscriptions[] = {
    {"current", weatherkit_current::Group::Current},
    {"forecast_minutes_5", weatherkit_current::Group::Minutes5},
    {"forecast_minutes_30", weatherkit_current::Group::Minutes30},
    {"forecast_hours_1", weatherkit_current::Group::Hour1},
    {"forecast_hours_2", weatherkit_current::Group::Hour2},
    {"forecast_hours_6", weatherkit_current::Group::Hour6},
    {"forecast_hours_12", weatherkit_current::Group::Hour12},
    {"forecast_hours_24", weatherkit_current::Group::Hour24},
    {"forecast_days_1", weatherkit_current::Group::Day1},
    {"forecast_days_2", weatherkit_current::Group::Day2},
    {"forecast_days_5", weatherkit_current::Group::Day5},
};
bool configured = false;
bool clockSynchronized = false;
bool clientStopped = false;
bool startAttempted = false;
bool lastConnectionState = false;
int outstandingMessageId = 0;
uint32_t outstandingAt = 0;
uint32_t lastPublishAt = 0;
uint32_t lastStartAttemptAt = 0;
bool healthStarted = false;
bool transportReady = false;
bool haveAcknowledgement = false;
bool publishProblem = false;
bool publishTimedOut = false;
uint32_t unavailableSince = 0;

struct MqttDiagnostics {
  uint32_t connects = 0;
  uint32_t disconnects = 0;
  uint32_t errors = 0;
  bool connected = false;
  bool outageActive = false;
  uint32_t outageAt = 0;
  uint32_t lastOutageMs = 0;
  uint32_t longestOutageMs = 0;
  bool errorDetails = false;
  int errorType = 0;
  int espError = 0;
  int tlsError = 0;
  int certFlags = 0;
  int socketError = 0;
  int refusalCode = 0;
  uint32_t errorAt = 0;
};
std::mutex diagnosticLock;
MqttDiagnostics mqttDiagnostics;
uint32_t reportedErrors = 0;
uint32_t acknowledgedPublishes = 0;
uint32_t acknowledgementTimeouts = 0;
uint32_t enqueueFailures = 0;
uint32_t rejectedGroups = 0;
uint32_t droppedLogs = 0;
size_t queueHighWater = 0;
std::string pendingDiagnostic;
bool startupDiagnosticPending = true;
bool recoveryDiagnosticPending = false;
uint32_t diagnosticLastAt = 0;
uint32_t diagnosticRecoveryCount = 0;
uint32_t diagnosticCoalesced = 0;

struct RateResult {
  bool valid = false;
  int status = 0;
  uint32_t limit = 0;
  uint32_t active = 0;
  uint32_t at = 0;
};
std::mutex rateLock;
RateResult rateResult;
bool rateResultPending = false;
bool rateWorkerRunning = false;
RateResult lastRate;
bool rateAttempted = false;
uint32_t rateAttemptAt = 0;
bool rateRefreshRequested = true;
uint32_t rateFailures = 0;
uint32_t throttleEvents = 0;
bool throttlePaused = false;
uint32_t throttleAt = 0;

bool parseRate(const char* body, size_t length, RateResult& result) {
  JsonDocument document;
  if (length > 512 || deserializeJson(document, body, length)) {
    return false;
  }
  const JsonVariantConst limit = document["data_rate_limit"];
  const JsonVariantConst active = document["active_data_rate"];
  if (!limit.is<uint32_t>() || !active.is<uint32_t>() ||
      limit.as<uint32_t>() == 0) {
    return false;
  }
  result.limit = limit.as<uint32_t>();
  result.active = active.as<uint32_t>();
  return true;
}

class RateBody : public Stream {
 public:
  char data[512] = {};
  size_t length = 0;
  bool overflow = false;

  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t size) override {
    const size_t copied = size < sizeof(data) - length ? size : sizeof(data) - length;
    memcpy(data + length, bytes, copied);
    length += copied;
    overflow = overflow || copied != size;
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

RateResult fetchRate() {
  RateResult result;
  WiFiClientSecure transport;
  transport.setCACert(kRootCa);
  transport.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  const std::string url =
      std::string("https://io.adafruit.com/api/v2/") + AIO_USERNAME + "/throttle";
  if (http.begin(transport, url.c_str())) {
    http.addHeader("X-AIO-Key", AIO_KEY);
    result.status = http.GET();
    if (result.status == HTTP_CODE_OK) {
      RateBody body;
      const int read = http.writeToStream(&body);
      result.valid = read >= 0 && !body.overflow &&
          parseRate(body.data, body.length, result);
    }
    http.end();
  }
  result.at = millis();
  return result;
}

void rateTask(void*) {
  const RateResult result = fetchRate();
  {
    std::lock_guard<std::mutex> guard(rateLock);
    rateResult = result;
    rateResultPending = true;
    rateWorkerRunning = false;
  }
  vTaskDelete(nullptr);
}

void pausePublishing(uint32_t now) {
  throttlePaused = true;
  throttleAt = now;
}

void updateRate(bool ready, uint32_t now) {
  RateResult result;
  bool received = false;
  {
    std::lock_guard<std::mutex> guard(rateLock);
    if (rateResultPending) {
      result = rateResult;
      rateResultPending = false;
      received = true;
    }
  }
  if (received) {
    if (result.valid) {
      lastRate = result;
      Serial.printf("AIO rate: active=%lu limit=%lu\n",
                    static_cast<unsigned long>(result.active),
                    static_cast<unsigned long>(result.limit));
      if (result.active >= result.limit) {
        pausePublishing(now);
        Serial.println("AIO: account rate exhausted; new publishes paused for 60 seconds");
      }
    } else {
      ++rateFailures;
      Serial.printf("AIO ERROR: account rate check failed (HTTP=%d; transport or invalid response)\n",
                    result.status);
      if (result.status == 429) {
        pausePublishing(now);
      }
    }
  }
  if (throttlePaused && now - throttleAt >= kThrottlePauseMs) {
    throttlePaused = false;
    rateRefreshRequested = true;
    Serial.println("AIO: throttle cooldown elapsed; resuming paced publishes");
  }
  if (!ready || (rateAttempted &&
      now - rateAttemptAt < (rateRefreshRequested ? kRateRetryMs : kRatePollMs))) {
    return;
  }
  {
    std::lock_guard<std::mutex> guard(rateLock);
    if (rateWorkerRunning) {
      return;
    }
    rateWorkerRunning = true;
  }
  rateAttempted = true;
  rateAttemptAt = now;
  rateRefreshRequested = false;
  if (xTaskCreate(rateTask, "aio-rate", 8192, nullptr, 1, nullptr) != pdPASS) {
    std::lock_guard<std::mutex> guard(rateLock);
    rateWorkerRunning = false;
    ++rateFailures;
    Serial.println("AIO ERROR: unable to start account rate check task");
  }
}

void trackQueueHighWater() {
  const size_t size = pending.size() + pendingLogs.size();
  if (size > queueHighWater) {
    queueHighWater = size;
  }
}

void recordDisconnected(uint32_t now) {
  std::lock_guard<std::mutex> guard(diagnosticLock);
  if (mqttDiagnostics.connected) {
    ++mqttDiagnostics.disconnects;
    mqttDiagnostics.connected = false;
    mqttDiagnostics.outageActive = true;
    mqttDiagnostics.outageAt = now;
  }
}

void onMqttEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  const esp_mqtt_event_handle_t event =
      static_cast<esp_mqtt_event_handle_t>(eventData);
  switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
    case MQTT_EVENT_CONNECTED: {
      {
        std::lock_guard<std::mutex> guard(diagnosticLock);
        ++mqttDiagnostics.connects;
        mqttDiagnostics.connected = true;
        if (mqttDiagnostics.outageActive) {
          mqttDiagnostics.lastOutageMs = millis() - mqttDiagnostics.outageAt;
          if (mqttDiagnostics.lastOutageMs > mqttDiagnostics.longestOutageMs) {
            mqttDiagnostics.longestOutageMs = mqttDiagnostics.lastOutageMs;
          }
          mqttDiagnostics.outageActive = false;
        }
      }
      mqttConnected.store(true);
      const int errorsId = esp_mqtt_client_subscribe(event->client, errorsTopic, 0);
      const int throttleId =
          esp_mqtt_client_subscribe(event->client, throttleTopic, 0);
      if (errorsId < 0 || throttleId < 0) {
        mqttError.store(-1);
      }
      for (WeatherSubscription& subscription : weatherSubscriptions) {
        if (!subscription.topic.empty() &&
            esp_mqtt_client_subscribe(event->client, subscription.topic.c_str(), 0) < 0) {
          subscription.failed.store(true);
        }
      }
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
      recordDisconnected(millis());
      mqttConnected.store(false);
      weatherkit_current::disconnected();
      break;
    case MQTT_EVENT_PUBLISHED:
      acknowledgedMessageId.store(event->msg_id);
      break;
    case MQTT_EVENT_ERROR: {
      std::lock_guard<std::mutex> guard(diagnosticLock);
      ++mqttDiagnostics.errors;
      mqttDiagnostics.errorAt = millis();
      mqttDiagnostics.errorDetails = event->error_handle != nullptr;
      if (event->error_handle) {
        const esp_mqtt_error_codes_t& error = *event->error_handle;
        mqttDiagnostics.errorType = static_cast<int>(error.error_type);
        const bool transport = error.error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT;
        mqttDiagnostics.espError = transport ? error.esp_tls_last_esp_err : 0;
        mqttDiagnostics.tlsError = transport ? error.esp_tls_stack_err : 0;
        mqttDiagnostics.certFlags = transport ? error.esp_tls_cert_verify_flags : 0;
        mqttDiagnostics.socketError = transport ? error.esp_transport_sock_errno : 0;
        mqttDiagnostics.refusalCode =
            error.error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED
                ? static_cast<int>(error.connect_return_code) : 0;
      } else {
        mqttDiagnostics.errorType = -1;
        mqttDiagnostics.espError = 0;
        mqttDiagnostics.tlsError = 0;
        mqttDiagnostics.certFlags = 0;
        mqttDiagnostics.socketError = 0;
        mqttDiagnostics.refusalCode = 0;
      }
      break;
    }
    case MQTT_EVENT_DATA: {
      weatherkit_current::Group group = weatherkit_current::Group::None;
      for (const WeatherSubscription& subscription : weatherSubscriptions) {
        if (!subscription.topic.empty() && event->topic_len > 0 &&
            static_cast<size_t>(event->topic_len) == subscription.topic.size() &&
            memcmp(event->topic, subscription.topic.data(), subscription.topic.size()) == 0) {
          group = subscription.group;
          break;
        }
      }
      weatherkit_current::receive(
          group, event->data, static_cast<size_t>(event->data_len),
          static_cast<size_t>(event->current_data_offset),
          static_cast<size_t>(event->total_data_len));
      if (event->current_data_offset == 0 && event->topic_len > 0) {
        if (static_cast<size_t>(event->topic_len) == strlen(errorsTopic) &&
            memcmp(event->topic, errorsTopic, strlen(errorsTopic)) == 0) {
          brokerReportedError.store(true);
        } else if (static_cast<size_t>(event->topic_len) ==
                       strlen(throttleTopic) &&
                   memcmp(event->topic, throttleTopic,
                          strlen(throttleTopic)) == 0) {
          {
            std::lock_guard<std::mutex> guard(diagnosticLock);
            ++brokerThrottleEvents;
            const size_t length = event->data_len > 0
                ? static_cast<size_t>(event->data_len) : 0;
            const size_t copied = length < sizeof(brokerThrottleText) - 1
                ? length : sizeof(brokerThrottleText) - 1;
            if (copied) {
              memcpy(brokerThrottleText, event->data, copied);
            }
            brokerThrottleText[copied] = '\0';
          }
          brokerReportedThrottle.store(true);
        }
      }
      break;
    }
    default:
      break;
  }
}

bool validSample(const FeedSample& sample) {
  if (!sample.key || !sample.key[0] || !sample.value) {
    return false;
  }
  const size_t keyLength = strlen(sample.key);
  const size_t valueLength = strlen(sample.value);
  if (keyLength > kMaxFeedKeyLength || valueLength > kMaxValueLength) {
    return false;
  }
  for (size_t i = 0; i < keyLength; ++i) {
    const char ch = sample.key[i];
    if (!(isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' ||
          ch == '.')) {
      return false;
    }
  }
  return true;
}

void formatTimestamp(char (&stamp)[32]) {
  snprintf(stamp, sizeof(stamp), "TIME_UNSYNCED");
  if (clockSynchronized) {
    const time_t eventTime = time(nullptr);
    struct tm local;
    if (localtime_r(&eventTime, &local) == nullptr ||
        strftime(stamp, sizeof(stamp), "%Y%m%d%H%M%S", &local) == 0) {
      Serial.println("AIO ERROR: unable to format local log timestamp");
      snprintf(stamp, sizeof(stamp), "TIME_UNSYNCED");
    }
  }
}

void queueLog(const char* message) {
  if (pendingLogs.size() >= kMaxLogEntries) {
    ++droppedLogs;
    Serial.printf("AIO: aggregator.log queue full; dropped log entry: %s\n",
                  message);
    return;
  }
  char stamp[32];
  formatTimestamp(stamp);
  pendingLogs.push_back(std::string(stamp) + " " + message);
  trackQueueHighWater();
}

void queueMqttLog(const char* message) {
  Serial.printf("AIO: %s\n", message);
  queueLog(message);
}

bool startClient() {
  esp_mqtt_client_config_t config = {};
  config.uri = kBrokerUri;
  config.username = AIO_USERNAME;
  config.password = AIO_KEY;
  config.cert_pem = kRootCa;
  config.keepalive = 60;
  config.buffer_size = 4096;
  config.task_stack = 8192;
  config.reconnect_timeout_ms = 15000;

  client = esp_mqtt_client_init(&config);
  if (!client) {
    Serial.println("AIO ERROR: MQTT client initialization failed");
    return false;
  }
  const esp_err_t registered =
      esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, onMqttEvent,
                                     nullptr);
  if (registered != ESP_OK) {
    Serial.printf("AIO ERROR: MQTT event registration failed: %s\n",
                  esp_err_to_name(registered));
    esp_mqtt_client_destroy(client);
    client = nullptr;
    return false;
  }
  const esp_err_t result = esp_mqtt_client_start(client);
  if (result != ESP_OK) {
    Serial.printf("AIO ERROR: MQTT client start failed: %s\n",
                  esp_err_to_name(result));
    esp_mqtt_client_destroy(client);
    client = nullptr;
    return false;
  }
  Serial.println("AIO: MQTT client started");
  return true;
}

bool resetClient() {
  // Clear the MQTT-owned QoS 1 outbox before resubmitting the application head.
  if (!clientStopped) {
    const esp_err_t stopped = esp_mqtt_client_stop(client);
    if (stopped != ESP_OK) {
      Serial.printf("AIO ERROR: unable to stop timed-out MQTT client: %s\n",
                    esp_err_to_name(stopped));
      return false;
    }
    clientStopped = true;
  }
  const esp_err_t destroyed = esp_mqtt_client_destroy(client);
  if (destroyed != ESP_OK) {
    Serial.printf("AIO ERROR: unable to destroy timed-out MQTT client: %s\n",
                  esp_err_to_name(destroyed));
    return false;
  }
  client = nullptr;
  clientStopped = false;
  mqttConnected.store(false);
  recordDisconnected(millis());
  acknowledgedMessageId.store(0);
  outstandingMessageId = 0;
  return true;
}

void completePublish() {
  if (pending.empty()) {
    outstandingMessageId = 0;
    return;
  }
  Publish published = pending.front();
  pending.pop_front();
  outstandingMessageId = 0;

  if (published.isLog || published.isDiagnostic) {
    Serial.printf("AIO: %s published\n",
                  published.isDiagnostic ? kDiagnosticFeed : kLogFeed);
    return;
  }
  PublishedValue& previous = publishedValues.at(published.topic);
  previous.value = published.value;
  previous.acknowledged = true;
  if (published.isGroupEnd) {
    char message[kMaxGroupNameLength + 64];
    snprintf(message, sizeof(message), "Sent feed group %s - Number Updated: %u",
             published.groupName.c_str(),
             static_cast<unsigned>(published.groupUpdateCount));
    queueMqttLog(message);
  }
}

void promoteLog() {
  if (pendingLogs.empty() || pending.size() >= kMaxQueuedPublishes) {
    return;
  }
  const std::string message = pendingLogs.front();
  char topic[160];
  snprintf(topic, sizeof(topic), "%s/feeds/%s", AIO_USERNAME, kLogFeed);
  pending.push_back({topic, message, "", false, true, false, 0, false});
  pendingLogs.pop_front();
}

MqttDiagnostics diagnosticSnapshot() {
  std::lock_guard<std::mutex> guard(diagnosticLock);
  return mqttDiagnostics;
}

bool captureDiagnostic(const char* reason, bool wifiConnected, uint32_t now,
                       const MqttDiagnostics& mqtt) {
  char ages[352] = {};
  size_t used = 0;
  for (const WeatherSubscription& subscription : weatherSubscriptions) {
    const auto source = weatherkit_current::sourceStatus(subscription.group, now);
    char age[16] = "-";
    if (subscription.topic.empty() || !source.enabled) {
      snprintf(age, sizeof(age), "off");
    } else if (source.received) {
      snprintf(age, sizeof(age), "%lu", static_cast<unsigned long>(source.ageSeconds));
    }
    const int written = snprintf(ages + used, sizeof(ages) - used, "%s=%s ",
                                 subscription.type, age);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(ages) - used) {
      Serial.println("AIO ERROR: diagnostic source ages exceed buffer");
      return false;
    }
    used += static_cast<size_t>(written);
  }
  char stamp[32];
  formatTimestamp(stamp);
  char summary[kMaxDiagnosticBytes + 1];
  auto replaceable = pending.end();
  for (auto it = pending.begin(); it != pending.end(); ++it) {
    if (it->isDiagnostic &&
        !(it == pending.begin() && outstandingMessageId != 0)) {
      replaceable = it;
      break;
    }
  }
  const uint32_t coalesced = diagnosticCoalesced +
      (replaceable != pending.end() ? 1 : 0) + (!pendingDiagnostic.empty() ? 1 : 0);
  const uint32_t outageMs = mqtt.outageActive ? now - mqtt.outageAt : 0;
  const uint32_t longestMs =
      outageMs > mqtt.longestOutageMs ? outageMs : mqtt.longestOutageMs;
  const auto drops = weatherkit_current::discardStatus();
  char rateAge[16] = "-";
  if (lastRate.valid) {
    snprintf(rateAge, sizeof(rateAge), "%lu",
             static_cast<unsigned long>((now - lastRate.at) / 1000));
  }
  const int written = snprintf(
      summary, sizeof(summary),
      "%s %s up_s=%lld reset=%d\n"
      "mqtt=%s c/d/e=%lu/%lu/%lu outage_s current/last/max=%lu/%lu/%lu\n"
      "ack=%lu timeout=%lu enq_fail=%lu group_defer=%lu "
      "queue=%u log_wait=%u high=%u log_drop=%lu diag_coalesced=%lu\n"
      "inflight_id=%d age_s=%lu\n"
      "rate=%lu/%lu age_s=%s fail=%lu throttle=%lu pause_s=%lu\n"
      "last_error details=%s type=%d esp=%d tls=%d cert=0x%x errno=%d refusal=%d age_s=%lu\n"
      "wk_drop=%lu assembly/raw/invalid/change=%lu/%lu/%lu/%lu age_s %s\n"
      "wifi=%s rssi=%ld heap=%lu min=%lu",
      stamp, reason, static_cast<long long>(esp_timer_get_time() / 1000000),
      static_cast<int>(esp_reset_reason()),
      mqtt.connected ? "up" : "down",
      static_cast<unsigned long>(mqtt.connects),
      static_cast<unsigned long>(mqtt.disconnects),
      static_cast<unsigned long>(mqtt.errors),
      static_cast<unsigned long>(outageMs / 1000),
      static_cast<unsigned long>(mqtt.lastOutageMs / 1000),
      static_cast<unsigned long>(longestMs / 1000),
      static_cast<unsigned long>(acknowledgedPublishes),
      static_cast<unsigned long>(acknowledgementTimeouts),
      static_cast<unsigned long>(enqueueFailures),
      static_cast<unsigned long>(rejectedGroups),
      static_cast<unsigned>(pending.size()), static_cast<unsigned>(pendingLogs.size()),
      static_cast<unsigned>(queueHighWater),
      static_cast<unsigned long>(droppedLogs),
      static_cast<unsigned long>(coalesced),
      outstandingMessageId,
      static_cast<unsigned long>(outstandingMessageId ? (now - outstandingAt) / 1000 : 0),
      static_cast<unsigned long>(lastRate.active),
      static_cast<unsigned long>(lastRate.limit),
      rateAge,
      static_cast<unsigned long>(rateFailures),
      static_cast<unsigned long>(throttleEvents),
      static_cast<unsigned long>(throttlePaused && now - throttleAt < kThrottlePauseMs
          ? (kThrottlePauseMs - (now - throttleAt)) / 1000 : 0),
      mqtt.errorDetails ? "yes" : "no", mqtt.errorType, mqtt.espError,
      mqtt.tlsError, static_cast<unsigned>(mqtt.certFlags), mqtt.socketError,
      mqtt.refusalCode,
      static_cast<unsigned long>(mqtt.errors ? (now - mqtt.errorAt) / 1000 : 0),
      static_cast<unsigned long>(weatherkit_current::discardedMessages()),
      static_cast<unsigned long>(drops.assembly),
      static_cast<unsigned long>(drops.rawQueue),
      static_cast<unsigned long>(drops.invalid),
      static_cast<unsigned long>(drops.changeQueue), ages,
      wifiConnected ? "up" : "down", static_cast<long>(WiFi.RSSI()),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(ESP.getMinFreeHeap()));
  if (written < 0 || static_cast<size_t>(written) >= sizeof(summary)) {
    Serial.println("AIO ERROR: diagnostic summary exceeds 1024 bytes; not queued");
    return false;
  }
  // Only replace snapshots not yet handed to MQTT; never mutate an in-flight value.
  if (replaceable != pending.end()) {
    pending.erase(replaceable);
  }
  diagnosticCoalesced = coalesced;
  pendingDiagnostic = summary;
  Serial.printf("AIO: aggregator.diag captured (%s, %u bytes)\n",
                reason, static_cast<unsigned>(written));
  return true;
}

void updateDiagnostics(bool wifiConnected) {
  const MqttDiagnostics mqtt = diagnosticSnapshot();
  const uint32_t now = millis();
  if (mqtt.connects != diagnosticRecoveryCount) {
    if (mqtt.connects > 1) {
      recoveryDiagnosticPending = true;
    }
    diagnosticRecoveryCount = mqtt.connects;
  }
  const char* reason = nullptr;
  if (startupDiagnosticPending && mqtt.connected) {
    reason = "startup";
  } else if (now - diagnosticLastAt >= kDiagnosticIntervalMs) {
    reason = "periodic";
  } else if (recoveryDiagnosticPending && mqtt.connected &&
             now - diagnosticLastAt >= kRecoveryDiagnosticCooldownMs) {
    reason = "recovery";
  }
  if (reason && captureDiagnostic(reason, wifiConnected, now, mqtt)) {
    if (mqtt.connected) {
      startupDiagnosticPending = false;
    }
    recoveryDiagnosticPending = false;
    diagnosticLastAt = now;
  }
  if (pendingDiagnostic.empty() ||
      pending.size() + pendingLogs.size() >= kMaxQueuedPublishes) {
    return;
  }
  for (const Publish& publication : pending) {
    if (publication.isDiagnostic) {
      return;
    }
  }
  char topic[160];
  snprintf(topic, sizeof(topic), "%s/feeds/%s", AIO_USERNAME, kDiagnosticFeed);
  pending.push_back({topic, std::move(pendingDiagnostic), "", false, false, false, 0, true});
  pendingDiagnostic.clear();
  trackQueueHighWater();
}

}

void begin() {
  if (AIO_USERNAME[0] == '\0' || AIO_KEY[0] == '\0') {
    Serial.println("AIO: publishing disabled; configure AIO_USERNAME and AIO_KEY");
    return;
  }
  if (strlen(AIO_USERNAME) > 80) {
    Serial.println("AIO ERROR: AIO_USERNAME is too long for MQTT feed topics");
    return;
  }
  configured = true;
  diagnosticLastAt = millis();
  snprintf(errorsTopic, sizeof(errorsTopic), "%s/errors", AIO_USERNAME);
  snprintf(throttleTopic, sizeof(throttleTopic), "%s/throttle", AIO_USERNAME);
  if (AIO_WEATHER_LOCATION_ID[0]) {
    for (WeatherSubscription& subscription : weatherSubscriptions) {
      subscription.topic = std::string(AIO_USERNAME) + "/integration/weather/" +
                           AIO_WEATHER_LOCATION_ID + "/" + subscription.type;
    }
    Serial.println("WeatherKit: current, m5, m30, hour1, hour2, hour6, hour12, hour24, day1, day2, day5 enabled; first data waits for next MQTT push");
  } else {
    Serial.println("WeatherKit: disabled; configure AIO_WEATHER_LOCATION_ID");
  }
}

void update(bool wifiConnected, bool timeSynchronized, uint32_t now) {
  if (!configured) {
    return;
  }
  clockSynchronized = timeSynchronized;
  weatherkit_current::update(now);

  if (!client && wifiConnected && timeSynchronized &&
      (!startAttempted || now - lastStartAttemptAt >= kMqttStartRetryMs)) {
    startAttempted = true;
    lastStartAttemptAt = now;
    startClient();
  }

  const bool connected = mqttConnected.load();
  const bool ready = wifiConnected && timeSynchronized && connected;
  if (!healthStarted || (transportReady && !ready)) {
    unavailableSince = now;
    healthStarted = true;
  }
  transportReady = ready;
  if (!ready) {
    haveAcknowledgement = false;
  }
  if (connected != lastConnectionState) {
    lastConnectionState = connected;
    if (connected) {
      queueMqttLog("MQTT connected");
    } else {
      rateRefreshRequested = true;
      queueMqttLog("MQTT disconnected");
    }
  }

  const int error = mqttError.exchange(0);
  const MqttDiagnostics mqtt = diagnosticSnapshot();
  if (mqtt.errors != reportedErrors) {
    reportedErrors = mqtt.errors;
    char message[224];
    if (!mqtt.errorDetails) {
      snprintf(message, sizeof(message), "MQTT error: no details");
    } else if (mqtt.errorType == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      snprintf(message, sizeof(message),
               "MQTT transport error: esp=%d (%s) tls=%d cert=0x%x errno=%d",
               mqtt.espError, esp_err_to_name(mqtt.espError), mqtt.tlsError,
               static_cast<unsigned>(mqtt.certFlags), mqtt.socketError);
    } else if (mqtt.errorType == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
      snprintf(message, sizeof(message), "MQTT connection refused: code=%d",
               mqtt.refusalCode);
    } else {
      snprintf(message, sizeof(message), "MQTT error type %d", mqtt.errorType);
    }
    queueMqttLog(message);
  }
  for (WeatherSubscription& subscription : weatherSubscriptions) {
    if (subscription.failed.exchange(false)) {
      Serial.printf("WeatherKit %s ERROR: MQTT subscription enqueue failed; "
                    "retrying in 15 seconds\n", subscription.type);
      subscription.retry = true;
      subscription.lastRetryAt = now;
    }
    if (subscription.retry && connected &&
        now - subscription.lastRetryAt >= kMqttStartRetryMs) {
      subscription.lastRetryAt = now;
      if (esp_mqtt_client_subscribe(client, subscription.topic.c_str(), 0) >= 0) {
        subscription.retry = false;
        Serial.printf("WeatherKit %s: subscription retry queued\n", subscription.type);
      } else {
        Serial.printf("WeatherKit %s ERROR: subscription retry failed\n", subscription.type);
      }
    }
  }
  if (error != 0) {
    if (error == -1) {
      queueMqttLog("MQTT error/throttle subscription failed");
    } else {
      char message[64];
      snprintf(message, sizeof(message), "MQTT error type %d", error - 1);
      queueMqttLog(message);
    }
  }
  if (brokerReportedError.exchange(false)) {
    publishProblem = true;
    Serial.println("AIO ERROR: Adafruit IO reported an account error");
  }
  if (brokerReportedThrottle.exchange(false)) {
    {
      std::lock_guard<std::mutex> guard(diagnosticLock);
      throttleEvents = brokerThrottleEvents;
      Serial.printf("AIO throttle notice (first 191 bytes): %s\n", brokerThrottleText);
    }
    pausePublishing(now);
    rateRefreshRequested = true;
    publishProblem = true;
    Serial.println("AIO ERROR: Adafruit IO reported publish throttling; new publishes paused for 60 seconds");
  }
  updateRate(wifiConnected && timeSynchronized, now);

  const int ackId = acknowledgedMessageId.exchange(0);
  if (ackId != 0 && ackId != outstandingMessageId) {
    Serial.printf("AIO ACK unmatched: id=%d expected=%d\n", ackId, outstandingMessageId);
  }
  if (ackId != 0 && ackId == outstandingMessageId) {
    Serial.printf("AIO ACK: id=%d age_ms=%lu topic=%s\n", ackId,
                  static_cast<unsigned long>(now - outstandingAt),
                  pending.empty() ? "-" : pending.front().topic.c_str());
    ++acknowledgedPublishes;
    completePublish();
    haveAcknowledgement = true;
    publishProblem = false;
    publishTimedOut = false;
  }

  if (outstandingMessageId != 0 &&
      now - outstandingAt >= kPublishTimeoutMs) {
    publishTimedOut = true;
    ++acknowledgementTimeouts;
    Serial.printf("AIO ERROR: publish acknowledgement timed out; resetting MQTT "
                  "(id=%d age_ms=%lu topic=%s)\n", outstandingMessageId,
                  static_cast<unsigned long>(now - outstandingAt),
                  pending.empty() ? "-" : pending.front().topic.c_str());
    if (!pending.empty() && !pending.front().isLog &&
        !pending.front().failureLogged) {
      char message[224];
      snprintf(message, sizeof(message),
               "Publish acknowledgement timed out; retrying id=%d topic=%s",
               outstandingMessageId, pending.front().topic.c_str());
      queueLog(message);
      pending.front().failureLogged = true;
    }
    if (!resetClient()) {
      outstandingAt = static_cast<uint32_t>(now);
    }
    updateDiagnostics(wifiConnected);
    return;
  }

  promoteLog();
  updateDiagnostics(wifiConnected);
  if (!wifiConnected || !connected || pending.empty() ||
      outstandingMessageId != 0 ||
      throttlePaused ||
      now - lastPublishAt < kPublishIntervalMs) {
    return;
  }

  Publish& next = pending.front();
  const int messageId = esp_mqtt_client_enqueue(
      client, next.topic.c_str(), next.value.c_str(),
      static_cast<int>(next.value.size()), 1, 0, true);
  if (messageId < 0) {
    ++enqueueFailures;
    publishProblem = true;
    if (!next.failureLogged) {
      Serial.printf("AIO ERROR: unable to queue publish to %s; will retry\n",
                    next.topic.c_str());
      next.failureLogged = true;
      if (!next.isLog) {
        queueLog("Publish queue rejected a feed update; retrying");
      }
    }
    lastPublishAt = static_cast<uint32_t>(now);
    return;
  }
  outstandingMessageId = messageId;
  outstandingAt = static_cast<uint32_t>(now);
  lastPublishAt = static_cast<uint32_t>(now);
  uint32_t fingerprint = 2166136261u;
  for (unsigned char byte : next.value) {
    fingerprint = (fingerprint ^ byte) * 16777619u;
  }
  Serial.printf("AIO TX: id=%d at_ms=%lu topic=%s bytes=%u hash=%08lx\n",
                messageId, static_cast<unsigned long>(now), next.topic.c_str(),
                static_cast<unsigned>(next.value.size()),
                static_cast<unsigned long>(fingerprint));
}

HealthStatus health(uint32_t now) {
  if (!configured) {
    return HealthStatus::Off;
  }
  if (publishTimedOut) {
    return HealthStatus::Red;
  }
  if (!transportReady || !mqttConnected.load()) {
    return healthStarted && now - unavailableSince >= kPublishTimeoutMs
               ? HealthStatus::Red : HealthStatus::Amber;
  }
  if (!haveAcknowledgement || publishProblem || throttlePaused ||
      (outstandingMessageId != 0 && now - outstandingAt >= 10000)) {
    return HealthStatus::Amber;
  }
  return HealthStatus::Green;
}

HealthStatus weatherHealth(uint32_t now) {
  if (!configured || !AIO_WEATHER_LOCATION_ID[0]) {
    return HealthStatus::Off;
  }
  return weatherkit_current::health(transportReady && mqttConnected.load(), now);
}

bool enqueueGroup(const char* groupName, const FeedSample* samples,
                  size_t sampleCount) {
  if (!configured) {
    Serial.println("AIO ERROR: cannot enqueue feed group; AIO is not configured");
    return false;
  }
  if (!groupName || !groupName[0] || strlen(groupName) > kMaxGroupNameLength ||
      !samples || sampleCount == 0 || sampleCount > kMaxQueuedPublishes) {
    Serial.println("AIO ERROR: invalid feed group");
    return false;
  }
  for (size_t i = 0; i < sampleCount; ++i) {
    if (!validSample(samples[i])) {
      Serial.printf("AIO ERROR: invalid feed sample at index %u\n",
                    static_cast<unsigned>(i));
      return false;
    }
  }

  std::deque<Publish> changed;
  std::map<std::string, PublishedValue> newFeeds;
  for (size_t i = 0; i < sampleCount; ++i) {
    char topic[160];
    snprintf(topic, sizeof(topic), "%s/feeds/%s", AIO_USERNAME,
             samples[i].key);
    const std::string* effectiveValue = nullptr;
    // The latest queued value wins over acknowledged state, including reversions.
    for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
      if (it->topic == topic) {
        effectiveValue = &it->value;
        break;
      }
    }
    if (!effectiveValue) {
      for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
        if (!it->isLog && it->topic == topic) {
          effectiveValue = &it->value;
          break;
        }
      }
    }
    const auto previous = publishedValues.find(topic);
    if (!effectiveValue && previous != publishedValues.end() &&
        previous->second.acknowledged) {
      effectiveValue = &previous->second.value;
    }
    if (effectiveValue && *effectiveValue == samples[i].value) {
      continue;
    }
    changed.push_back({topic, samples[i].value, groupName, false, false, false, 0, false});
    if (previous == publishedValues.end()) {
      newFeeds.emplace(topic, PublishedValue{});
    }
  }
  if (changed.empty()) {
    Serial.printf("AIO: feed group %s unchanged; nothing queued\n", groupName);
    return true;
  }
  if (pending.size() + pendingLogs.size() + changed.size() + 1 >
      kMaxQueuedPublishes) {
    ++rejectedGroups;
    Serial.println("AIO ERROR: publish queue is full; feed group deferred for retry");
    return false;
  }
  if (publishedValues.size() + newFeeds.size() > kMaxTrackedFeeds) {
    ++rejectedGroups;
    Serial.println("AIO ERROR: change cache is full; feed group deferred for retry");
    return false;
  }
  changed.back().isGroupEnd = true;
  changed.back().groupUpdateCount = changed.size();
  publishedValues.insert(newFeeds.begin(), newFeeds.end());
  pending.insert(pending.end(), changed.begin(), changed.end());
  trackQueueHighWater();
  return true;
}

}
