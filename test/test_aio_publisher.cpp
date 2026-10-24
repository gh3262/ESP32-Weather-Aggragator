// Exercise the production implementation with a deterministic MQTT transport.
#define CREDENTIALS_H
#define AIO_USERNAME "test-user"
#define AIO_KEY "test-key"
#include "../src/aio_publisher.cpp"

#include <cassert>
#include <vector>

TestSerial Serial;

namespace {
struct Sent {
  std::string topic;
  std::string value;
  uint32_t at;
  int id;
};
std::vector<Sent> sent;
uint32_t clockMs = 0;
int nextId = 1;
int stops = 0;
int destroys = 0;
int subscriptionCalls = 0;
bool rejectPublish = false;
bool rejectSubscription = false;
bool rejectStop = false;
bool rejectDestroy = false;
TestMqttCallback callback = nullptr;
int fakeClient;

void event(esp_mqtt_event_id_t type, int id = 0) {
  esp_mqtt_event_t data = {};
  data.client = &fakeClient;
  data.msg_id = id;
  assert(callback);
  callback(nullptr, nullptr, type, &data);
}

void tick(uint32_t now) {
  clockMs = now;
  aio_publisher::update(true, true, now);
}

void reset(uint32_t now = 0) {
  using namespace aio_publisher;
  pending.clear();
  pendingLogs.clear();
  mqttConnected = false;
  acknowledgedMessageId = 0;
  mqttError = 0;
  brokerReportedError = false;
  brokerReportedThrottle = false;
  client = nullptr;
  configured = false;
  clientStopped = false;
  startAttempted = false;
  lastConnectionState = false;
  outstandingMessageId = 0;
  outstandingAt = 0;
  lastPublishAt = 0;
  lastStartAttemptAt = 0;
  sent.clear();
  nextId = 1;
  stops = destroys = subscriptionCalls = 0;
  rejectPublish = rejectSubscription = rejectStop = rejectDestroy = false;
  Serial.output.clear();
  begin();
  tick(now);
  event(MQTT_EVENT_CONNECTED);
  tick(now);
}

void acknowledge() {
  assert(!sent.empty());
  event(MQTT_EVENT_PUBLISHED, sent.back().id);
}
}

const char* esp_err_to_name(esp_err_t) { return "test error"; }
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config) {
  assert(std::string(config->uri) == "mqtts://io.adafruit.com:8883");
  assert(config->cert_pem && config->cert_pem[0]);
  return &fakeClient;
}
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t,
                                       esp_mqtt_event_id_t, TestMqttCallback cb,
                                       void*) {
  callback = cb;
  return ESP_OK;
}
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t) { return ESP_OK; }
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t) {
  ++stops;
  return rejectStop ? -1 : ESP_OK;
}
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t) {
  ++destroys;
  return rejectDestroy ? -1 : ESP_OK;
}
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char*, int) {
  ++subscriptionCalls;
  return rejectSubscription ? -1 : nextId++;
}
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char* topic,
                            const char* value, int length, int qos, int retain,
                            bool store) {
  assert(qos == 1 && retain == 0 && store);
  if (rejectPublish) {
    return -1;
  }
  const int id = nextId++;
  sent.push_back({topic, std::string(value, static_cast<size_t>(length)),
                  clockMs, id});
  return id;
}

int main() {
  using namespace aio_publisher;
  const FeedSample samples[] = {{"weather.temp", "72"}, {"weather.wind", "5"}};
  begin();
  update(false, false, 0);
  assert(client == nullptr);
  update(true, false, 1000);
  assert(client == nullptr);
  update(false, true, 2000);
  assert(client == nullptr);

  reset();
  assert(enqueueGroup("current", samples, 2));
  tick(1999);
  assert(sent.empty());
  tick(2000);
  assert(sent.size() == 1 && sent.back().topic == "test-user/feeds/aggregator.log");
  acknowledge();
  tick(2001);
  tick(3999);
  assert(sent.size() == 1);
  tick(4000);
  assert(sent.size() == 2 && sent.back().value == "72");
  event(MQTT_EVENT_PUBLISHED, sent.back().id + 100);
  tick(4001);
  assert(outstandingMessageId == sent.back().id);
  acknowledge();
  tick(4002);
  tick(6000);
  assert(sent.size() == 3 && sent.back().value == "5");
  acknowledge();
  tick(6001);
  tick(8000);
  assert(sent.size() == 4 && sent.back().value == "Sent feed group current");

  reset();
  FeedSample burst[60];
  for (auto& sample : burst) {
    sample = {"weather.temp", "72"};
  }
  assert(enqueueGroup("burst", burst, 60));
  for (uint32_t now = 1; now <= 120000; ++now) {
    tick(now);
    if (outstandingMessageId != 0) {
      acknowledge();
    }
  }
  assert(sent.size() == 60);
  for (size_t i = 1; i < sent.size(); ++i) {
    assert(sent[i].at - sent[i - 1].at >= 2000);
    size_t inWindow = 0;
    for (const auto& publication : sent) {
      if (publication.at <= sent[i].at && sent[i].at - publication.at < 60000) {
        ++inWindow;
      }
    }
    assert(inWindow <= 30);
  }

  reset();
  tick(2000);
  const int originalId = sent.back().id;
  event(MQTT_EVENT_DISCONNECTED);
  tick(2001);
  event(MQTT_EVENT_CONNECTED);
  tick(4000);
  assert(sent.size() == 1 && outstandingMessageId == originalId);
  event(MQTT_EVENT_PUBLISHED, originalId);
  tick(4001);
  assert(outstandingMessageId != originalId);
  assert(stops == 0);

  reset();
  tick(2000);
  tick(31999);
  assert(sent.size() == 1 && stops == 0);
  tick(32000);
  assert(sent.size() == 1 && stops == 1 && destroys == 1 && client == nullptr);
  tick(32001);
  event(MQTT_EVENT_CONNECTED);
  tick(32002);
  assert(sent.size() == 2 && sent.back().value == "MQTT connected");

  reset();
  tick(2000);
  rejectStop = true;
  tick(32000);
  assert(stops == 1 && destroys == 0 && outstandingMessageId != 0);
  tick(32001);
  assert(stops == 1 && sent.size() == 1);

  reset();
  tick(2000);
  rejectDestroy = true;
  tick(32000);
  assert(stops == 1 && destroys == 1 && clientStopped);
  rejectDestroy = false;
  tick(62000);
  assert(stops == 1 && destroys == 2 && client == nullptr);

  reset();
  rejectPublish = true;
  tick(2000);
  assert(sent.empty() && pending.size() == 1);
  rejectPublish = false;
  tick(3999);
  assert(sent.empty());
  tick(4000);
  assert(sent.size() == 1);

  reset();
  const size_t queued = pending.size();
  const FeedSample invalid[] = {{"valid", "1"}, {"bad/key", "2"}};
  assert(!enqueueGroup("invalid", invalid, 2) && pending.size() == queued);
  assert(!enqueueGroup("empty", samples, 0) && pending.size() == queued);
  std::string oversized(513, 'x');
  const FeedSample tooLong[] = {{"valid", oversized.c_str()}};
  assert(!enqueueGroup("oversized", tooLong, 1) && pending.size() == queued);
  assert(enqueueGroup("full", burst, 60));
  assert(!enqueueGroup("overflow", burst, 60));

  reset(UINT32_MAX - 1000);
  assert(sent.size() == 1);
  acknowledge();
  tick(UINT32_MAX - 999);
  queueLog("wraparound");
  tick(998);
  assert(sent.size() == 1);
  tick(999);
  assert(sent.size() == 2);

  reset();
  rejectSubscription = true;
  subscriptionCalls = 0;
  event(MQTT_EVENT_CONNECTED);
  assert(subscriptionCalls == 2);
  tick(1);
  assert(Serial.output.find("subscription failed") != std::string::npos);

  puts("AIO publisher tests passed.");
}
