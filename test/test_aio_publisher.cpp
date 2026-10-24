// Exercise the production implementation with a deterministic MQTT transport.
#include <time.h>
time_t testTime(time_t*);
struct tm* testLocalTime(const time_t*, struct tm*);
#define time testTime
#define localtime_r testLocalTime
#define CREDENTIALS_H
#define AIO_USERNAME "test-user"
#define AIO_KEY "test-key"
#define AIO_WEATHER_LOCATION_ID "test-location"
#include "../src/aio_publisher.cpp"
#undef time
#undef localtime_r
#include "../src/weatherkit_current.cpp"

#include <cassert>
#include <vector>

TestSerial Serial;

time_t testTime(time_t*) { return 1792857600; }
struct tm* testLocalTime(const time_t*, struct tm* local) {
  *local = {};
  local->tm_year = 126;
  local->tm_mon = 9;
  local->tm_mday = 24;
  local->tm_hour = 11;
  local->tm_min = 4;
  local->tm_sec = 42;
  return local;
}

namespace {
struct Sent {
  std::string topic;
  std::string value;
  uint32_t at;
  int id;
};
std::vector<Sent> sent;
std::vector<std::string> subscribedTopics;
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
  publishedValues.clear();
  mqttConnected = false;
  acknowledgedMessageId = 0;
  mqttError = 0;
  brokerReportedError = false;
  brokerReportedThrottle = false;
  for (auto& subscription : weatherSubscriptions) {
    subscription.failed = false;
    subscription.retry = false;
    subscription.lastRetryAt = 0;
  }
  weatherkit_current::incoming.clear();
  weatherkit_current::assembly.clear();
  weatherkit_current::assembling = false;
  weatherkit_current::receiveFailures = 0;
  weatherkit_current::waitingSamples.clear();
  weatherkit_current::retryPending = false;
  client = nullptr;
  configured = false;
  clockSynchronized = false;
  clientStopped = false;
  startAttempted = false;
  lastConnectionState = false;
  outstandingMessageId = 0;
  outstandingAt = 0;
  lastPublishAt = 0;
  lastStartAttemptAt = 0;
  sent.clear();
  subscribedTopics.clear();
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
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char* topic, int) {
  ++subscriptionCalls;
  subscribedTopics.push_back(topic);
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
  assert(subscribedTopics.size() == 6);
  assert(subscribedTopics[2] ==
         "test-user/integration/weather/test-location/current");
  assert(subscribedTopics[3] ==
         "test-user/integration/weather/test-location/forecast_minutes_5");
  assert(subscribedTopics[4] ==
         "test-user/integration/weather/test-location/forecast_minutes_30");
  assert(subscribedTopics[5] ==
         "test-user/integration/weather/test-location/forecast_hours_1");
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
  assert(sent.size() == 4 && sent.back().value ==
         "20261024110442 Sent feed group current - Number Updated: 2");

  reset();
  FeedSample burst[60];
  char burstKeys[60][32];
  for (size_t i = 0; i < 60; ++i) {
    std::snprintf(burstKeys[i], sizeof(burstKeys[i]), "weather.feed%u",
                  static_cast<unsigned>(i));
    burst[i] = {burstKeys[i], "72"};
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
  assert(subscribedTopics.size() == 12);
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
  assert(sent.size() == 2 && sent.back().value == "20261024110442 MQTT connected");

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
  const FeedSample overflow[] = {
      {"new.feed", "1"}, {"new.other", "2"}, {"new.third", "3"}};
  assert(!enqueueGroup("overflow", overflow, 3));
  assert(publishedValues.count("test-user/feeds/new.feed") == 0);
  assert(enqueueGroup("full-repeat", burst, 60));

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
  assert(subscriptionCalls == 6);
  tick(1);
  assert(Serial.output.find("subscription failed") != std::string::npos);
  for (const auto& subscription : weatherSubscriptions) {
    assert(subscription.retry);
  }
  rejectSubscription = false;
  tick(15001);
  for (const auto& subscription : weatherSubscriptions) {
    assert(!subscription.retry);
  }

  reset();
  pending.clear();
  pendingLogs.clear();
  assert(enqueueGroup("first", samples, 2));
  assert(publishedValues.size() == 2);
  assert(!publishedValues.at("test-user/feeds/weather.temp").acknowledged);
  assert(enqueueGroup("repeat-before-send", samples, 2));
  assert(pending.size() == 2);
  tick(2000);
  assert(sent.back().value == "72");
  assert(enqueueGroup("repeat-in-flight", samples, 2));
  assert(pending.size() == 2);
  rejectPublish = true;
  acknowledge();
  tick(2001);
  assert(publishedValues.at("test-user/feeds/weather.temp").acknowledged);
  assert(publishedValues.at("test-user/feeds/weather.temp").value == "72");
  rejectPublish = false;
  tick(4000);
  acknowledge();
  tick(4001);
  const size_t afterAck = pending.size();
  assert(enqueueGroup("repeat-after-ack", samples, 2));
  assert(pending.size() == afterAck);
  const FeedSample mixed[] = {{"weather.temp", "73"}, {"weather.wind", "5"}};
  assert(enqueueGroup("mixed", mixed, 2));
  assert(pending.size() == afterAck + 1);
  assert(pending.back().value == "73" && pending.back().isGroupEnd);
  assert(pending.back().groupUpdateCount == 1);
  assert(publishedValues.at("test-user/feeds/weather.temp").value == "72");
  const FeedSample reversion[] = {{"weather.temp", "72"}};
  assert(enqueueGroup("reversion", reversion, 1));
  assert(pending.back().value == "72");
  assert(pending.size() == afterAck + 2);
  assert(enqueueGroup("repeat-reversion", reversion, 1));
  assert(pending.size() == afterAck + 2);
  tick(6000);
  acknowledge();
  tick(6001);
  tick(8000);
  assert(sent.back().value == "73");
  acknowledge();
  tick(8001);
  assert(publishedValues.at("test-user/feeds/weather.temp").value == "73");
  tick(10000);
  assert(sent.back().value == "72");
  acknowledge();
  tick(10001);
  assert(publishedValues.at("test-user/feeds/weather.temp").value == "72");

  reset();
  pending.clear();
  pendingLogs.clear();
  const FeedSample sequence[] = {
      {"weather.temp", "0"}, {"weather.temp", "0"}, {"weather.temp", "1"},
      {"weather.temp", "0"}, {"weather.temp", "0"}};
  assert(enqueueGroup("sequence", sequence, 5));
  assert(pending.size() == 3 && publishedValues.size() == 1);
  assert(!pending[0].isGroupEnd && !pending[1].isGroupEnd &&
         pending[2].isGroupEnd);
  assert(pending[2].groupUpdateCount == 3);
  tick(2000);
  rejectPublish = true;
  tick(32000);
  assert(!publishedValues.at("test-user/feeds/weather.temp").acknowledged);
  tick(32001);
  event(MQTT_EVENT_CONNECTED);
  rejectPublish = false;
  tick(32002);
  acknowledge();
  tick(32003);
  assert(publishedValues.at("test-user/feeds/weather.temp").value == "0");

  reset();
  pending.clear();
  pendingLogs.clear();
  const FeedSample invalidTail[] = {{"new.feed", "1"}, {"bad/key", "2"}};
  assert(!enqueueGroup("invalid", invalidTail, 2));
  assert(publishedValues.empty() && pending.empty());
  assert(enqueueGroup("format", samples, 2));
  const FeedSample formatted[] = {{"weather.temp", "72.0"}};
  assert(enqueueGroup("format-change", formatted, 1));
  assert(pending.size() == 3);
  for (size_t i = publishedValues.size(); i < kMaxTrackedFeeds; ++i) {
    publishedValues.emplace("reserved-" + std::to_string(i), PublishedValue{});
  }
  const size_t beforeFull = pending.size();
  const FeedSample cacheOverflow[] = {{"weather.temp", "74"}, {"untracked", "1"}};
  assert(!enqueueGroup("cache-full", cacheOverflow, 2));
  assert(pending.size() == beforeFull && publishedValues.size() == kMaxTrackedFeeds);
  assert(enqueueGroup("known-feed", reversion, 1));

  reset();
  pending.clear();
  pendingLogs.clear();
  assert(enqueueGroup("send-failure", samples, 2));
  rejectPublish = true;
  tick(2000);
  assert(!publishedValues.at("test-user/feeds/weather.temp").acknowledged);
  assert(enqueueGroup("retry-same-values", samples, 2));
  assert(pending.size() == 2);
  rejectPublish = false;
  tick(4000);
  acknowledge();
  tick(4001);
  assert(publishedValues.at("test-user/feeds/weather.temp").acknowledged);
  assert(!publishedValues.at("test-user/feeds/weather.wind").acknowledged);

  reset();
  pending.clear();
  pendingLogs.clear();
  assert(enqueueGroup("after-reboot", samples, 2));
  assert(pending.size() == 2);
  queueLog("repeated event");
  queueLog("repeated event");
  assert(pendingLogs.size() == 2);

  reset();
  pending.clear();
  pendingLogs.clear();
  const FeedSample manualTest[] = {
      {"aggregator.diag", "publisher-test:1:A"},
      {"aggregator.diag", "publisher-test:1:A"},
      {"aggregator.diag", "publisher-test:1:B"},
      {"aggregator.diag", "publisher-test:1:A"}};
  assert(enqueueGroup("publisher-test", manualTest, 4));
  assert(pending.size() == 3);
  for (uint32_t now = 2000; now <= 8000; now += 2000) {
    tick(now);
    acknowledge();
    tick(now + 1);
  }
  assert(sent.size() == 4);
  assert(sent[0].topic == "test-user/feeds/aggregator.diag");
  assert(sent[0].value == "publisher-test:1:A");
  assert(sent[1].value == "publisher-test:1:B");
  assert(sent[2].value == "publisher-test:1:A");
  assert(sent[3].topic == "test-user/feeds/aggregator.log");
  assert(sent[3].value ==
         "20261024110442 Sent feed group publisher-test - Number Updated: 3");
  assert(pending.empty() && pendingLogs.empty());

  reset();
  pending.clear();
  pendingLogs.clear();
  const std::string currentJson =
      R"({"temperature":20,"humidity":0.65,"windDirection":225})";
  esp_mqtt_event_t message = {};
  message.client = &fakeClient;
  message.topic = &weatherSubscriptions[0].topic[0];
  message.topic_len = static_cast<int>(weatherSubscriptions[0].topic.size());
  std::string part = currentJson.substr(0, 10);
  message.data = &part[0];
  message.data_len = static_cast<int>(part.size());
  message.total_data_len = static_cast<int>(currentJson.size());
  callback(nullptr, nullptr, MQTT_EVENT_DATA, &message);
  tick(1);
  assert(pending.empty());
  part = currentJson.substr(10);
  message.topic = nullptr;
  message.topic_len = 0;
  message.current_data_offset = 10;
  message.data = &part[0];
  message.data_len = static_cast<int>(part.size());
  callback(nullptr, nullptr, MQTT_EVENT_DATA, &message);
  tick(2);
  assert(pending.size() == 4);
  for (uint32_t now = 2000; now <= 10000; now += 2000) {
    tick(now);
    acknowledge();
    tick(now + 1);
  }
  assert(sent.size() == 5);
  assert(sent[0].topic == "test-user/feeds/currenthumidity" && sent[0].value == "65");
  assert(sent[1].topic == "test-user/feeds/currenttemperature" && sent[1].value == "68.0");
  assert(sent[2].topic == "test-user/feeds/currentwinddegrees" && sent[2].value == "225");
  assert(sent[3].topic == "test-user/feeds/currentwinddirection" && sent[3].value == "SW");
  assert(sent[4].value ==
         "20261024110442 Sent feed group WeatherKit current - Number Updated: 4");
  weatherkit_current::receive(weatherkit_current::Group::Current,
                             currentJson.data(), currentJson.size(), 0,
                             currentJson.size());
  tick(12000);
  assert(sent.size() == 5 && pending.empty());

  reset();
  pending.clear();
  pendingLogs.clear();
  clockSynchronized = false;
  queueLog("before NTP");
  assert(pendingLogs.front() == "TIME_UNSYNCED before NTP");
  clockSynchronized = true;
  queueLog("after NTP");
  assert(pendingLogs.back() == "20261024110442 after NTP");
  tick(2000);
  assert(sent.back().value == "TIME_UNSYNCED before NTP");
  acknowledge();
  tick(2001);
  tick(4000);
  assert(sent.back().value == "20261024110442 after NTP");

  reset();
  pending.clear();
  pendingLogs.clear();
  auto minuteEvent = [&](size_t subscriptionIndex, std::string json) {
    esp_mqtt_event_t data = {};
    data.client = &fakeClient;
    data.topic = &weatherSubscriptions[subscriptionIndex].topic[0];
    data.topic_len = static_cast<int>(weatherSubscriptions[subscriptionIndex].topic.size());
    data.data = &json[0];
    data.data_len = static_cast<int>(json.size());
    data.total_data_len = data.data_len;
    callback(nullptr, nullptr, MQTT_EVENT_DATA, &data);
  };
  const std::string minuteJson =
      R"({"startTime":"2026-10-24T16:45:00Z","precipitationChance":0.5,"precipitationIntensity":0})";
  minuteEvent(1, minuteJson);
  minuteEvent(2, minuteJson);
  tick(1);
  tick(2);
  assert(pending.size() == 6);
  for (uint32_t now = 2000; now <= 16000; now += 2000) {
    tick(now);
    acknowledge();
    tick(now + 1);
  }
  assert(sent.size() == 8);
  assert(sent[0].topic == "test-user/feeds/m5starttime");
  assert(sent[1].topic == "test-user/feeds/m5precipitationchance" &&
         sent[1].value == "50");
  assert(sent[2].topic == "test-user/feeds/m5precipitationintensity" &&
         sent[2].value == "0");
  assert(sent[3].topic == "test-user/feeds/m30starttime");
  assert(sent[4].topic == "test-user/feeds/m30precipitationchance");
  assert(sent[5].topic == "test-user/feeds/m30precipitationintensity");
  assert(sent[6].value ==
         "20261024110442 Sent feed group WeatherKit m5 - Number Updated: 3");
  assert(sent[7].value ==
         "20261024110442 Sent feed group WeatherKit m30 - Number Updated: 3");
  minuteEvent(1, minuteJson);
  minuteEvent(2, minuteJson);
  tick(16002);
  tick(16003);
  assert(pending.empty() && pendingLogs.empty() && sent.size() == 8);
  minuteEvent(1, R"({"startTime":"2026-10-24T16:45:00Z","precipitationChance":0.75,"precipitationIntensity":0})");
  tick(16004);
  assert(pending.size() == 1 && pending.front().value == "75");
  tick(18000);
  acknowledge();
  tick(18001);
  tick(20000);
  assert(sent.back().value ==
         "20261024110442 Sent feed group WeatherKit m5 - Number Updated: 1");

  reset();
  pending.clear();
  pendingLogs.clear();
  minuteEvent(3, R"({"forecastStart":"2026-10-24T18:00:00Z","daylight":false,
                   "visibility":10000,"temperature":20})");
  tick(1);
  assert(pending.size() == 4);
  for (uint32_t now = 2000; now <= 10000; now += 2000) {
    tick(now);
    acknowledge();
    tick(now + 1);
  }
  assert(sent[0].topic == "test-user/feeds/hours1start");
  assert(sent[1].value == "It will be dark");
  assert(sent[2].value == "68.0");
  assert(sent[3].value == "10000");
  assert(sent[4].value ==
         "20261024110442 Sent feed group WeatherKit hour1 - Number Updated: 4");
  minuteEvent(3, R"({"forecastStart":"2026-10-24T18:00:00Z","daylight":false,
                   "visibility":10000,"temperature":20})");
  tick(10002);
  assert(pending.empty() && pendingLogs.empty() && sent.size() == 5);

  puts("AIO publisher tests passed.");
}
