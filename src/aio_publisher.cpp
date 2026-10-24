#include "aio_publisher.h"
#include "weatherkit_current.h"

#include <Arduino.h>
#include <mqtt_client.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
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
constexpr size_t kMaxQueuedPublishes = 64;
constexpr size_t kMaxFeedKeyLength = 64;
constexpr size_t kMaxValueLength = 512;
constexpr size_t kMaxGroupNameLength = 64;
constexpr size_t kMaxLogEntries = 16;
constexpr size_t kMaxTrackedFeeds = 256;
constexpr char kLogFeed[] = "aggregator.log";
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

void onMqttEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  const esp_mqtt_event_handle_t event =
      static_cast<esp_mqtt_event_handle_t>(eventData);
  switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
    case MQTT_EVENT_CONNECTED: {
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
      mqttConnected.store(false);
      weatherkit_current::disconnected();
      break;
    case MQTT_EVENT_PUBLISHED:
      acknowledgedMessageId.store(event->msg_id);
      break;
    case MQTT_EVENT_ERROR:
      if (event->error_handle) {
        mqttError.store(static_cast<int>(event->error_handle->error_type) + 1);
      } else {
        mqttError.store(-1);
      }
      break;
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

void queueLog(const char* message) {
  if (pendingLogs.size() >= kMaxLogEntries) {
    Serial.printf("AIO: aggregator.log queue full; dropped log entry: %s\n",
                  message);
    return;
  }
  char stamp[32] = "TIME_UNSYNCED";
  if (clockSynchronized) {
    const time_t eventTime = time(nullptr);
    struct tm local;
    if (localtime_r(&eventTime, &local) == nullptr ||
        strftime(stamp, sizeof(stamp), "%Y%m%d%H%M%S", &local) == 0) {
      Serial.println("AIO ERROR: unable to format local log timestamp");
      snprintf(stamp, sizeof(stamp), "TIME_UNSYNCED");
    }
  }
  pendingLogs.push_back(std::string(stamp) + " " + message);
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

  if (published.isLog) {
    Serial.printf("AIO: aggregator.log published\n");
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
  pending.push_back({topic, message, "", false, true, false, 0});
  pendingLogs.pop_front();
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
  snprintf(errorsTopic, sizeof(errorsTopic), "%s/errors", AIO_USERNAME);
  snprintf(throttleTopic, sizeof(throttleTopic), "%s/throttle", AIO_USERNAME);
  if (AIO_WEATHER_LOCATION_ID[0]) {
    for (WeatherSubscription& subscription : weatherSubscriptions) {
      subscription.topic = std::string(AIO_USERNAME) + "/integration/weather/" +
                           AIO_WEATHER_LOCATION_ID + "/" + subscription.type;
    }
    Serial.println("WeatherKit: current, m5, m30, hour1 enabled; first data waits for next MQTT push");
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
  if (connected != lastConnectionState) {
    lastConnectionState = connected;
    if (connected) {
      queueMqttLog("MQTT connected");
    } else {
      queueMqttLog("MQTT disconnected");
    }
  }

  const int error = mqttError.exchange(0);
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
    Serial.println("AIO ERROR: Adafruit IO reported an account error");
  }
  if (brokerReportedThrottle.exchange(false)) {
    Serial.println("AIO ERROR: Adafruit IO reported publish throttling");
  }

  const int ackId = acknowledgedMessageId.exchange(0);
  if (ackId != 0 && ackId == outstandingMessageId) {
    completePublish();
  }

  if (outstandingMessageId != 0 &&
      now - outstandingAt >= kPublishTimeoutMs) {
    Serial.println("AIO ERROR: publish acknowledgement timed out; resetting MQTT");
    if (!pending.empty() && !pending.front().isLog &&
        !pending.front().failureLogged) {
      queueLog("Publish acknowledgement timed out; retrying");
      pending.front().failureLogged = true;
    }
    if (!resetClient()) {
      outstandingAt = static_cast<uint32_t>(now);
    }
    return;
  }

  promoteLog();
  if (!wifiConnected || !connected || pending.empty() ||
      outstandingMessageId != 0 ||
      now - lastPublishAt < kPublishIntervalMs) {
    return;
  }

  Publish& next = pending.front();
  const int messageId = esp_mqtt_client_enqueue(
      client, next.topic.c_str(), next.value.c_str(),
      static_cast<int>(next.value.size()), 1, 0, true);
  if (messageId < 0) {
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
    changed.push_back({topic, samples[i].value, groupName, false, false, false, 0});
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
    Serial.println("AIO ERROR: publish queue is full; feed group rejected");
    return false;
  }
  if (publishedValues.size() + newFeeds.size() > kMaxTrackedFeeds) {
    Serial.println("AIO ERROR: change cache is full; feed group rejected");
    return false;
  }
  changed.back().isGroupEnd = true;
  changed.back().groupUpdateCount = changed.size();
  publishedValues.insert(newFeeds.begin(), newFeeds.end());
  pending.insert(pending.end(), changed.begin(), changed.end());
  return true;
}

}
