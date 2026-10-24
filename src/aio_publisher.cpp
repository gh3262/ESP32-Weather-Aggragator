#include "aio_publisher.h"

#include <Arduino.h>
#include <mqtt_client.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
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
};

esp_mqtt_client_handle_t client = nullptr;
std::deque<Publish> pending;
std::deque<std::string> pendingLogs;
std::atomic<bool> mqttConnected{false};
std::atomic<int> acknowledgedMessageId{0};
std::atomic<int> mqttError{0};
std::atomic<bool> brokerReportedError{false};
std::atomic<bool> brokerReportedThrottle{false};
char errorsTopic[96] = {};
char throttleTopic[96] = {};
bool configured = false;
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
    case MQTT_EVENT_CONNECTED:
      mqttConnected.store(true);
      if (esp_mqtt_client_subscribe(event->client, errorsTopic, 0) < 0 ||
          esp_mqtt_client_subscribe(event->client, throttleTopic, 0) < 0) {
        mqttError.store(-1);
      }
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqttConnected.store(false);
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
    case MQTT_EVENT_DATA:
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
  pendingLogs.push_back(message);
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
  if (published.isGroupEnd) {
    char message[kMaxGroupNameLength + 32];
    snprintf(message, sizeof(message), "Sent feed group %s",
             published.groupName.c_str());
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
  pending.push_back({topic, message, "", false, true, false});
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
}

void update(bool wifiConnected, bool timeSynchronized, unsigned long now) {
  if (!configured) {
    return;
  }

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
      outstandingMessageId = 0;
      acknowledgedMessageId.store(0);
    }
  }

  const int error = mqttError.exchange(0);
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
    Serial.println("AIO ERROR: publish acknowledgement timed out; retrying");
    if (!pending.empty() && !pending.front().isLog) {
      queueLog("Publish acknowledgement timed out; retrying");
    }
    outstandingMessageId = 0;
  }

  promoteLog();
  if (!connected || pending.empty() || outstandingMessageId != 0 ||
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
      !samples || sampleCount == 0 || sampleCount > kMaxQueuedPublishes ||
      pending.size() + pendingLogs.size() + sampleCount + 1 >
          kMaxQueuedPublishes) {
    Serial.println("AIO ERROR: invalid feed group or publish queue is full");
    return false;
  }
  for (size_t i = 0; i < sampleCount; ++i) {
    if (!validSample(samples[i])) {
      Serial.printf("AIO ERROR: invalid feed sample at index %u\n",
                    static_cast<unsigned>(i));
      return false;
    }
  }

  for (size_t i = 0; i < sampleCount; ++i) {
    char topic[160];
    snprintf(topic, sizeof(topic), "%s/feeds/%s", AIO_USERNAME,
             samples[i].key);
    pending.push_back({topic, samples[i].value, groupName,
                       i + 1 == sampleCount, false, false});
  }
  return true;
}

}
