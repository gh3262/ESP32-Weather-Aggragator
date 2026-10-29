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
#include "weatherkit_day_fixture.h"

TestSerial Serial;
uint32_t testMillis = 0;
TestEsp ESP;
TestWiFi WiFi;
void (*testTask)(void*) = nullptr;
bool testTaskFailure = false;
int testHttpStatus = 200;
std::string testHttpBody = R"({"data_rate_limit":60,"active_data_rate":12})";
unsigned testHttpCalls = 0;

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
  testMillis = now;
  aio_publisher::update(true, true, now);
}

void reset(uint32_t now = 0, bool diagnostics = false) {
  using namespace aio_publisher;
  pending.clear();
  pendingLogs.clear();
  publishedValues.clear();
  mqttConnected = false;
  acknowledgedMessageId = 0;
  mqttError = 0;
  brokerReportedError = false;
  brokerReportedThrottle = false;
  brokerThrottleEvents = 0;
  brokerThrottleText[0] = '\0';
  rateResult = RateResult{};
  rateResultPending = rateWorkerRunning = false;
  https_gate::release();
  lastRate = RateResult{};
  rateAttempted = false;
  rateAttemptAt = 0;
  rateRefreshRequested = true;
  rateFailures = throttleEvents = 0;
  throttlePaused = false;
  throttleAt = 0;
  testTask = nullptr;
  testTaskFailure = false;
  testHttpStatus = 200;
  testHttpBody = R"({"data_rate_limit":60,"active_data_rate":12})";
  testHttpCalls = 0;
  for (auto& subscription : weatherSubscriptions) {
    subscription.failed = false;
    subscription.retry = false;
    subscription.lastRetryAt = 0;
  }
  weatherkit_current::incoming.clear();
  weatherkit_current::changes.clear();
  weatherkit_current::assembly.clear();
  weatherkit_current::assembling = false;
  weatherkit_current::receiveFailures = 0;
  weatherkit_current::discardedTotal = 0;
  weatherkit_current::rawOverflow = 0;
  weatherkit_current::assemblyFailures = 0;
  weatherkit_current::invalidPayloads = 0;
  weatherkit_current::changeOverflow = 0;
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
  healthStarted = false;
  transportReady = false;
  haveAcknowledgement = false;
  publishProblem = false;
  publishTimedOut = false;
  unavailableSince = 0;
  mqttDiagnostics = MqttDiagnostics{};
  reportedErrors = 0;
  acknowledgedPublishes = 0;
  acknowledgementTimeouts = 0;
  enqueueFailures = 0;
  rejectedGroups = 0;
  droppedLogs = 0;
  queueHighWater = 0;
  pendingDiagnostic.clear();
  startupDiagnosticPending = diagnostics;
  recoveryDiagnosticPending = false;
  diagnosticRecoveryCount = 0;
  diagnosticCoalesced = 0;
  weatherkit_current::healthStarted = false;
  weatherkit_current::everConnected = false;
  for (auto& source : weatherkit_current::sources) {
    source.received = false;
    source.lastReceivedAt = 0;
    source.acceptedValues.clear();
  }
  sent.clear();
  subscribedTopics.clear();
  nextId = 1;
  stops = destroys = subscriptionCalls = 0;
  rejectPublish = rejectSubscription = rejectStop = rejectDestroy = false;
  Serial.output.clear();
  testMillis = now;
  begin();
  tick(now);
  event(MQTT_EVENT_CONNECTED);
  tick(now);
}

void acknowledge() {
  assert(!sent.empty());
  event(MQTT_EVENT_PUBLISHED, sent.back().id);
}

void drain() {
  for (size_t i = 0; i < 320 &&
       (!aio_publisher::pending.empty() || !aio_publisher::pendingLogs.empty() ||
        !aio_publisher::pendingDiagnostic.empty()); ++i) {
    tick(clockMs + 2000);
    if (aio_publisher::outstandingMessageId != 0) {
      acknowledge();
      tick(clockMs + 1);
    }
  }
  assert(aio_publisher::pending.empty());
  assert(aio_publisher::pendingLogs.empty());
  assert(aio_publisher::pendingDiagnostic.empty());
}
}

const char* esp_err_to_name(esp_err_t) { return "test error"; }
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config) {
  assert(std::string(config->uri) == "mqtts://io.adafruit.com:8883");
  assert(config->cert_pem && config->cert_pem[0]);
  assert(config->message_retransmit_timeout > static_cast<int>(aio_publisher::kPublishTimeoutMs));
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

  RateResult parsed;
  const char* validRate = R"({"data_rate_limit":60,"active_data_rate":0})";
  assert(parseRate(validRate, strlen(validRate), parsed));
  assert(parsed.limit == 60 && parsed.active == 0);
  for (const char* invalidRate : {
      "bad", "[]", "{}", R"({"data_rate_limit":0,"active_data_rate":1})",
      R"({"data_rate_limit":60,"active_data_rate":-1})",
      R"({"data_rate_limit":60,"active_data_rate":1.5})",
      R"({"data_rate_limit":"60","active_data_rate":1})",
      R"({"data_rate_limit":60,"active_data_rate":null})",
      R"({"data_rate_limit":60,"active_data_rate":4294967296})"}) {
    assert(!parseRate(invalidRate, strlen(invalidRate), parsed));
  }
  const std::string oversizedRate(513, ' ');
  assert(!parseRate(oversizedRate.c_str(), oversizedRate.size(), parsed));
  RateBody boundedBody;
  assert(boundedBody.write(static_cast<uint8_t>('{')) == 1);
  assert(boundedBody.write(reinterpret_cast<const uint8_t*>(oversizedRate.data()),
                           oversizedRate.size()) == oversizedRate.size());
  assert(boundedBody.overflow && boundedBody.length == 512);
  assert(boundedBody.write(static_cast<uint8_t>('}')) == 1);
  assert(boundedBody.length == 512);

  reset();
  assert(testTask && rateWorkerRunning && testHttpCalls == 0);
  tick(2000);  // The HTTP worker has not run, but MQTT still progresses.
  assert(sent.size() == 1);
  acknowledge();
  tick(2001);
  testTask(nullptr);
  tick(2002);
  assert(lastRate.valid && lastRate.active == 12 && lastRate.limit == 60);
  assert(testHttpCalls == 1 && !rateWorkerRunning);
  tick(59999);
  assert(!rateWorkerRunning);
  tick(60000);
  assert(rateWorkerRunning);
  testHttpBody = R"({"data_rate_limit":60,"active_data_rate":60})";
  testTask(nullptr);
  tick(60001);
  assert(throttlePaused);
  assert(enqueueGroup("rate-test", samples, 2));
  tick(62000);
  assert(sent.size() == 1);
  assert(captureDiagnostic("periodic", true, 62000, diagnosticSnapshot()));
  assert(pendingDiagnostic.find("rate=60/60 age_s=2 fail=0 throttle=0 pause_s=58") != std::string::npos);
  tick(120001);
  assert(!throttlePaused && sent.size() == 2);
  acknowledge();
  tick(120002);
  assert(publishedValues.at("test-user/feeds/weather.temp").value == "72");

  reset();
  testHttpStatus = 503;
  testTask(nullptr);
  tick(1);
  assert(rateFailures == 1 && !lastRate.valid && !throttlePaused);
  assert(Serial.output.find("HTTP=503") != std::string::npos);
  tick(60000);
  testHttpStatus = 200;
  testHttpBody = "{}";
  testTask(nullptr);
  tick(60001);
  assert(rateFailures == 2 && !lastRate.valid);
  tick(120000);
  testHttpStatus = 429;
  testTask(nullptr);
  tick(120001);
  assert(rateFailures == 3 && throttlePaused);

  reset();
  testTask(nullptr);
  tick(1);
  tick(60000);
  testHttpStatus = -1;
  testTask(nullptr);
  tick(60001);
  assert(rateFailures == 1 && lastRate.valid && lastRate.at == 0);
  assert(captureDiagnostic("periodic", true, 60001, diagnosticSnapshot()));
  assert(pendingDiagnostic.find("rate=12/60 age_s=60 fail=1") != std::string::npos);
  tick(120000);
  testHttpStatus = 200;
  testHttpBody = oversizedRate;
  testTask(nullptr);
  tick(120001);
  assert(rateFailures == 2 && lastRate.active == 12);

  reset();
  testTask(nullptr);
  tick(1);
  testTaskFailure = true;
  tick(60000);
  assert(rateFailures == 1 && !rateWorkerRunning);
  tick(60001);
  assert(rateFailures == 1);
  assert(Serial.output.find("unable to start account rate check task") != std::string::npos);
  assert(std::string(https_gate::owner()) == "-");

  // The rate check waits for the shared HTTPS slot and releases it when done.
  reset();
  assert(std::string(https_gate::owner()) == "AIO rate");
  testTask(nullptr);
  assert(std::string(https_gate::owner()) == "-");
  tick(1);
  assert(https_gate::tryAcquire("OpenWeather"));
  tick(60000);
  assert(!rateWorkerRunning && rateAttemptAt == 0);
  https_gate::release();
  tick(60001);
  assert(rateWorkerRunning && rateAttemptAt == 60001);
  assert(std::string(https_gate::owner()) == "AIO rate");
  testTask(nullptr);
  tick(60002);

  reset(UINT32_MAX - 20000);
  tick(UINT32_MAX - 18000);
  assert(outstandingMessageId != 0);
  esp_mqtt_event_t throttle = {};
  throttle.topic = throttleTopic;
  throttle.topic_len = static_cast<int>(strlen(throttleTopic));
  char notice[] = "data rate limit reached, 9 seconds until throttle released";
  throttle.data = notice;
  throttle.data_len = throttle.total_data_len = static_cast<int>(strlen(notice));
  callback(nullptr, nullptr, MQTT_EVENT_DATA, &throttle);
  callback(nullptr, nullptr, MQTT_EVENT_DATA, &throttle);
  tick(UINT32_MAX - 17000);
  assert(throttleEvents == 2 && throttlePaused);
  assert(Serial.output.find(notice) != std::string::npos);
  acknowledge();
  tick(UINT32_MAX - 16999); // ACKs still drain during cooldown.
  assert(outstandingMessageId == 0);
  assert(health(UINT32_MAX - 16999) == HealthStatus::Amber);
  assert(enqueueGroup("throttle-test", samples, 2));
  tick(42998);
  assert(throttlePaused && sent.size() == 1);
  callback(nullptr, nullptr, MQTT_EVENT_DATA, &throttle);
  tick(42999); // New notices extend cooldown, including across millis wrap.
  tick(103000);
  assert(!throttlePaused && sent.size() == 2 && throttleEvents == 3);
  assert(sent.back().topic == "test-user/feeds/weather.temp");
  assert(Serial.output.find("AIO TX: id=") != std::string::npos);
  assert(Serial.output.find("AIO ACK: id=") != std::string::npos);

  reset();
  assert(subscribedTopics.size() == 13);
  assert(subscribedTopics[2] ==
         "test-user/integration/weather/test-location/current");
  assert(subscribedTopics[3] ==
         "test-user/integration/weather/test-location/forecast_minutes_5");
  assert(subscribedTopics[4] ==
         "test-user/integration/weather/test-location/forecast_minutes_30");
  assert(subscribedTopics[5] ==
         "test-user/integration/weather/test-location/forecast_hours_1");
  assert(subscribedTopics[6] ==
         "test-user/integration/weather/test-location/forecast_hours_2");
  assert(subscribedTopics[7] ==
         "test-user/integration/weather/test-location/forecast_hours_6");
  assert(subscribedTopics[8] ==
         "test-user/integration/weather/test-location/forecast_hours_12");
  assert(subscribedTopics[9] ==
         "test-user/integration/weather/test-location/forecast_hours_24");
  assert(subscribedTopics[10] ==
         "test-user/integration/weather/test-location/forecast_days_1");
  assert(subscribedTopics[11] ==
         "test-user/integration/weather/test-location/forecast_days_2");
  assert(subscribedTopics[12] ==
         "test-user/integration/weather/test-location/forecast_days_5");
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
  assert(subscribedTopics.size() == 26);
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
  assert(subscriptionCalls == 13);
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
         sent[2].value == "0.00");
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
  assert(sent[3].value == "6");
  assert(sent[4].value ==
         "20261024110442 Sent feed group WeatherKit hour1 - Number Updated: 4");
  minuteEvent(3, R"({"forecastStart":"2026-10-24T18:00:00Z","daylight":false,
                   "visibility":10000,"temperature":20})");
  tick(10002);
  assert(pending.empty() && pendingLogs.empty() && sent.size() == 5);

  reset();
  pending.clear();
  pendingLogs.clear();
  const std::string hour2Json = R"({"forecastStart":"2026-10-24T19:00:00Z",
    "daylight":false,"visibility":10000,"temperature":20})";
  minuteEvent(4, hour2Json);
  tick(1);
  assert(pending.size() == 4);
  drain();
  assert(sent[0].topic == "test-user/feeds/hours2start");
  assert(sent[1].value == "It will be dark");
  assert(sent[2].value == "68.0" && sent[3].value == "6");
  assert(sent[4].value ==
         "20261024110442 Sent feed group WeatherKit hour2 - Number Updated: 4");
  minuteEvent(4, hour2Json);
  tick(clockMs + 1);
  assert(pending.empty() && sent.size() == 5);
  minuteEvent(4, R"({"temperature":21})");
  tick(clockMs + 1);
  drain();
  assert(sent[5].topic == "test-user/feeds/hours2temperature");
  assert(sent[5].value == "69.8");
  assert(sent[6].value ==
         "20261024110442 Sent feed group WeatherKit hour2 - Number Updated: 1");

  reset();
  pending.clear();
  pendingLogs.clear();
  const std::string hour6Json = R"({"forecastStart":"2026-10-25T05:00:00Z",
    "daylight":false,"visibility":10000,"temperature":20})";
  minuteEvent(5, hour6Json);
  tick(1);
  assert(pending.size() == 4);
  drain();
  assert(sent[0].topic == "test-user/feeds/hours6start");
  assert(sent[1].value == "It will be dark");
  assert(sent[2].value == "68.0" && sent[3].value == "6");
  assert(sent[4].value ==
         "20261024110442 Sent feed group WeatherKit hour6 - Number Updated: 4");
  minuteEvent(5, hour6Json);
  tick(clockMs + 1);
  assert(pending.empty() && sent.size() == 5);
  minuteEvent(5, R"({"temperature":21})");
  tick(clockMs + 1);
  drain();
  assert(sent[5].topic == "test-user/feeds/hours6temperature");
  assert(sent[5].value == "69.8");
  assert(sent[6].value ==
         "20261024110442 Sent feed group WeatherKit hour6 - Number Updated: 1");

  reset();
  pending.clear();
  pendingLogs.clear();
  const std::string hour12Json = R"({"forecastStart":"2026-10-25T11:00:00Z",
    "daylight":false,"visibility":10000,"temperature":20})";
  minuteEvent(6, hour12Json);
  tick(1);
  assert(pending.size() == 4);
  drain();
  assert(sent[0].topic == "test-user/feeds/hours12start");
  assert(sent[1].value == "It will be dark");
  assert(sent[2].value == "68.0" && sent[3].value == "6");
  assert(sent[4].value ==
         "20261024110442 Sent feed group WeatherKit hour12 - Number Updated: 4");
  minuteEvent(6, hour12Json);
  tick(clockMs + 1);
  assert(pending.empty() && sent.size() == 5);
  minuteEvent(6, R"({"temperature":21})");
  tick(clockMs + 1);
  drain();
  assert(sent[5].topic == "test-user/feeds/hours12temperature");
  assert(sent[5].value == "69.8");
  assert(sent[6].value ==
         "20261024110442 Sent feed group WeatherKit hour12 - Number Updated: 1");

  reset();
  pending.clear();
  pendingLogs.clear();
  const std::string hour24Json = R"({"forecastStart":"2026-10-26T02:00:00Z",
    "daylight":false,"visibility":10000,"temperature":20})";
  minuteEvent(7, hour24Json);
  tick(1);
  assert(pending.size() == 4);
  drain();
  assert(sent[0].topic == "test-user/feeds/hours24start");
  assert(sent[1].value == "It will be dark");
  assert(sent[2].value == "68.0" && sent[3].value == "6");
  assert(sent[4].value ==
         "20261024110442 Sent feed group WeatherKit hour24 - Number Updated: 4");
  minuteEvent(7, hour24Json);
  tick(clockMs + 1);
  assert(pending.empty() && sent.size() == 5);
  minuteEvent(7, R"({"temperature":21})");
  tick(clockMs + 1);
  drain();
  assert(sent[5].topic == "test-user/feeds/hours24temperature");
  assert(sent[5].value == "69.8");
  assert(sent[6].value ==
         "20261024110442 Sent feed group WeatherKit hour24 - Number Updated: 1");

  reset();
  pending.clear();
  pendingLogs.clear();
  minuteEvent(8, kDay1Payload);
  tick(1);
  assert(pending.size() == 56);
  drain();
  assert(sent.size() == 57);
  assert(sent[0].topic == "test-user/feeds/days1start");
  assert(sent[56].value ==
         "20261024110442 Sent feed group WeatherKit day1 - Number Updated: 56");
  minuteEvent(8, kDay1Payload);
  tick(clockMs + 1);
  assert(pending.empty() && sent.size() == 57);
  minuteEvent(8, R"({"temperatureMax":21,"overnightForecast":{"temperatureMin":-11}})");
  tick(clockMs + 1);
  drain();
  assert(sent[57].topic == "test-user/feeds/days1temperaturemax");
  assert(sent[57].value == "69.8");
  assert(sent[58].topic == "test-user/feeds/days1ovnttemperaturemin");
  assert(sent[58].value == "12.2");
  assert(sent[59].value ==
         "20261024110442 Sent feed group WeatherKit day1 - Number Updated: 2");

  reset();
  pending.clear();
  pendingLogs.clear();
  minuteEvent(9, kDay1Payload);
  tick(1);
  assert(pending.size() == 56);
  drain();
  assert(sent.size() == 57);
  assert(sent[0].topic == "test-user/feeds/days2start");
  assert(sent[56].value ==
         "20261024110442 Sent feed group WeatherKit day2 - Number Updated: 56");
  minuteEvent(9, kDay1Payload);
  tick(clockMs + 1);
  assert(pending.empty() && sent.size() == 57);
  minuteEvent(9, R"({"temperatureMax":21,"overnightForecast":{"temperatureMin":-11}})");
  tick(clockMs + 1);
  drain();
  assert(sent[57].topic == "test-user/feeds/days2temperaturemax");
  assert(sent[57].value == "69.8");
  assert(sent[58].topic == "test-user/feeds/days2ovnttemperaturemin");
  assert(sent[58].value == "12.2");
  assert(sent[59].value ==
         "20261024110442 Sent feed group WeatherKit day2 - Number Updated: 2");

  reset();
  pending.clear();
  pendingLogs.clear();
  size_t sourceIndex = 0;
  std::vector<std::string> startupPayloads;
  for (const auto& source : weatherkit_current::sources) {
    JsonDocument document;
    for (size_t i = 0; i < source.count; ++i) {
      const auto& mapping = source.mappings[i];
      const char* separator = strchr(mapping.field, '.');
      JsonVariant field = separator
          ? document[std::string(mapping.field, separator - mapping.field)][separator + 1].to<JsonVariant>()
          : document[mapping.field].to<JsonVariant>();
      switch (mapping.conversion) {
        case feed_config::Conversion::Text:
          field.set("test");
          break;
        case feed_config::Conversion::Boolean:
        case feed_config::Conversion::ForecastDaylight:
          field.set(true);
          break;
        case feed_config::Conversion::Percent:
          field.set(0.5);
          break;
        default:
          field.set(1);
          break;
      }
    }
    std::string json;
    serializeJson(document, json);
    startupPayloads.push_back(json);
    minuteEvent(sourceIndex++, json);
  }
  for (uint32_t now = 1; now <= 10; ++now) {
    tick(now);
  }
  assert(weatherkit_current::waitingSamples.size() == 21);
  assert(weatherkit_current::retryPending);
  assert(rejectedGroups > 0);
  assert(weatherkit_current::discardedMessages() == 0);
  uint32_t nextPushAt = 60000;
  for (size_t cycle = 0; cycle < 400 &&
       (!pending.empty() || !pendingLogs.empty() ||
        !weatherkit_current::waitingSamples.empty() ||
        !weatherkit_current::changes.empty()); ++cycle) {
    const uint32_t now = clockMs + 2000;
    if (now >= nextPushAt) {
      testMillis = now;
      for (size_t i = 0; i < startupPayloads.size(); ++i) {
        minuteEvent(i, startupPayloads[i]);
      }
      nextPushAt += 60000;
    }
    tick(now);
    if (outstandingMessageId != 0) {
      acknowledge();
      tick(now + 1);
    }
  }
  assert(pending.empty() && pendingLogs.empty());
  assert(weatherkit_current::discardedMessages() == 0);
  size_t dataPublishes = 0;
  size_t completionLogs = 0;
  std::map<std::string, size_t> feeds;
  for (const auto& publication : sent) {
    if (publication.topic == "test-user/feeds/aggregator.log") {
      if (publication.value.find("Sent feed group WeatherKit") != std::string::npos) {
        ++completionLogs;
      }
    } else {
      ++dataPublishes;
      ++feeds[publication.topic];
    }
  }
  assert(dataPublishes == 299 && feeds.size() == 299);
  assert(completionLogs == 11);
  for (const auto& feed : feeds) {
    assert(feed.second == 1);
  }
  for (size_t i = 1; i < sent.size(); ++i) {
    assert(sent[i].at - sent[i - 1].at >= 2000);
  }
  assert(weatherkit_current::waitingSamples.empty());
  assert(!weatherkit_current::retryPending);

  reset();
  assert(health(0) == HealthStatus::Amber);
  tick(2000);
  acknowledge();
  tick(2001);
  assert(health(2001) == HealthStatus::Green);
  assert(enqueueGroup("health-test", samples, 2));
  tick(4000);
  tick(13999);
  assert(health(13999) == HealthStatus::Green);
  tick(14000);
  assert(health(14000) == HealthStatus::Amber);
  tick(34000);
  assert(health(34000) == HealthStatus::Red);
  tick(34001);
  event(MQTT_EVENT_CONNECTED);
  tick(34002);
  assert(health(34002) == HealthStatus::Red);
  acknowledge();
  tick(34003);
  assert(health(34003) == HealthStatus::Green);

  reset();
  tick(2000);
  acknowledge();
  tick(2001);
  brokerReportedThrottle = true;
  assert(weatherHealth(2001) == HealthStatus::Amber);
  tick(2002);
  assert(health(2002) == HealthStatus::Amber);
  assert(enqueueGroup("health-test", samples, 2));
  tick(4000);
  assert(sent.size() == 1 && outstandingMessageId == 0);
  tick(62001);
  assert(sent.size() == 1 && health(62001) == HealthStatus::Amber);
  tick(62002);
  assert(sent.size() == 2);
  acknowledge();
  tick(62003);
  assert(health(62003) == HealthStatus::Green);
  reset();
  assert(weatherHealth(0) == HealthStatus::Amber);
  tick(2000);
  acknowledge();
  tick(2001);
  assert(enqueueGroup("health-test", samples, 2));
  tick(4000);
  acknowledge();
  tick(4001);
  event(MQTT_EVENT_DISCONNECTED);
  tick(5000);
  assert(health(5000) == HealthStatus::Amber);
  assert(weatherHealth(5000) == HealthStatus::Red);
  tick(34999);
  assert(health(34999) == HealthStatus::Amber);
  tick(35000);
  assert(health(35000) == HealthStatus::Red);
  event(MQTT_EVENT_CONNECTED);
  tick(35001);
  assert(health(35001) == HealthStatus::Amber);
  acknowledge();
  tick(35002);
  assert(health(35002) == HealthStatus::Green);
  configured = false;
  assert(health(35003) == HealthStatus::Off);
  assert(weatherHealth(35003) == HealthStatus::Off);

  reset();
  tick(2000);
  acknowledge();
  tick(2001);
  assert(enqueueGroup("health-test", samples, 2));
  rejectPublish = true;
  tick(4000);
  assert(health(4000) == HealthStatus::Amber);
  rejectPublish = false;
  tick(6000);
  acknowledge();
  tick(6001);
  assert(health(6001) == HealthStatus::Green);

  reset(UINT32_MAX - 1000);
  assert(health(UINT32_MAX - 1000) == HealthStatus::Amber);
  acknowledge();
  tick(UINT32_MAX - 999);
  assert(health(UINT32_MAX - 999) == HealthStatus::Green);
  event(MQTT_EVENT_DISCONNECTED);
  tick(UINT32_MAX - 998);
  tick(29000);
  assert(health(29000) == HealthStatus::Amber);
  tick(29001);
  assert(health(29001) == HealthStatus::Red);

  reset(0, true);
  assert(pending.size() == 2);
  assert(pending.back().isDiagnostic);
  const std::string startup = pending.back().value;
  assert(startup.find("20261024110442 startup up_s=0 reset=1") == 0);
  assert(startup.find("mqtt=up c/d/e=1/0/0") != std::string::npos);
  assert(startup.find("forecast_hours_1=-") != std::string::npos);
  assert(startup.find("forecast_hours_2=-") != std::string::npos);
  assert(startup.find("forecast_hours_6=-") != std::string::npos);
  assert(startup.find("forecast_hours_12=-") != std::string::npos);
  assert(startup.find("forecast_hours_24=-") != std::string::npos);
  assert(startup.find("forecast_days_1=-") != std::string::npos);
  assert(startup.find("forecast_days_2=-") != std::string::npos);
  assert(startup.find("wifi=up rssi=-55 heap=200000 min=180000") != std::string::npos);
  assert(startup.size() <= 1024);
  drain();
  assert(sent.size() == 2);
  assert(sent[1].topic == "test-user/feeds/aggregator.diag");
  assert(sent[1].value == startup);
  assert(sent[1].at - sent[0].at >= 2000);
  assert(acknowledgedPublishes == 2);
  assert(publishedValues.empty());
  assert(Serial.output.find("aggregator.diag published") != std::string::npos);
  tick(kDiagnosticIntervalMs - 1);
  assert(sent.size() == 2);
  tick(kDiagnosticIntervalMs);
  assert(sent.size() == 3);
  assert(sent.back().value.find(" periodic ") != std::string::npos);
  acknowledge();
  tick(kDiagnosticIntervalMs + 1);
  assert(pending.empty() && pendingLogs.empty());

  reset(0, true);
  drain();
  tick(10000);
  event(MQTT_EVENT_DISCONNECTED);
  tick(12000);
  event(MQTT_EVENT_CONNECTED);
  tick(12001);
  assert(mqttDiagnostics.connects == 2 && mqttDiagnostics.disconnects == 1);
  assert(mqttDiagnostics.lastOutageMs == 2000);
  assert(recoveryDiagnosticPending);
  drain();
  const size_t beforeRecovery = sent.size();
  tick(kRecoveryDiagnosticCooldownMs - 1);
  assert(sent.size() == beforeRecovery);
  tick(kRecoveryDiagnosticCooldownMs);
  assert(sent.back().topic == "test-user/feeds/aggregator.diag");
  assert(sent.back().value.find(" recovery ") != std::string::npos);
  assert(sent.back().value.find("outage_s current/last/max=0/2/2") != std::string::npos);
  acknowledge();
  tick(kRecoveryDiagnosticCooldownMs + 1);
  // Even transitions occurring entirely between loop updates are counted.
  event(MQTT_EVENT_DISCONNECTED);
  testMillis += 1000;
  event(MQTT_EVENT_CONNECTED);
  tick(testMillis);
  assert(mqttDiagnostics.connects == 3 && mqttDiagnostics.disconnects == 2);
  assert(mqttDiagnostics.lastOutageMs == 1000);
  assert(mqttDiagnostics.longestOutageMs == 2000);
  assert(recoveryDiagnosticPending);
  drain();
  const size_t beforeCooldown = sent.size();
  tick(119999);
  assert(sent.size() == beforeCooldown);
  tick(120000);
  assert(sent.back().value.find(" recovery ") != std::string::npos);
  assert(sent.back().value.find("c/d/e=3/2/0") != std::string::npos);

  reset();
  esp_mqtt_error_codes_t details = {};
  details.error_type = MQTT_ERROR_TYPE_TCP_TRANSPORT;
  details.esp_tls_last_esp_err = 32774;
  details.esp_tls_stack_err = -29184;
  details.esp_tls_cert_verify_flags = 8;
  details.esp_transport_sock_errno = 119;
  esp_mqtt_event_t errorEvent = {};
  errorEvent.error_handle = &details;
  callback(nullptr, nullptr, MQTT_EVENT_ERROR, &errorEvent);
  callback(nullptr, nullptr, MQTT_EVENT_ERROR, &errorEvent);
  assert(mqttDiagnostics.errors == 2);
  assert(Serial.output.find("errno=119") == std::string::npos);
  details.esp_transport_sock_errno = 0;
  tick(1);
  assert(Serial.output.find("tls=-29184 cert=0x8 errno=119") != std::string::npos);
  assert(captureDiagnostic("periodic", true, 1001, diagnosticSnapshot()));
  assert(pendingDiagnostic.find("type=1 esp=32774 tls=-29184 cert=0x8 errno=119") !=
         std::string::npos);
  assert(pendingDiagnostic.find("age_s=1\nwk_drop=") != std::string::npos);
  details.error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED;
  details.connect_return_code = 5;
  callback(nullptr, nullptr, MQTT_EVENT_ERROR, &errorEvent);
  tick(2);
  assert(Serial.output.find("MQTT connection refused: code=5") != std::string::npos);
  errorEvent.error_handle = nullptr;
  callback(nullptr, nullptr, MQTT_EVENT_ERROR, &errorEvent);
  tick(3);
  assert(mqttDiagnostics.errors == 4);
  assert(Serial.output.find("MQTT error: no details") != std::string::npos);
  assert(captureDiagnostic("periodic", true, 3, diagnosticSnapshot()));
  assert(pendingDiagnostic.find("details=no type=-1 esp=0 tls=0 cert=0x0 errno=0") !=
         std::string::npos);

  reset(0, true);
  tick(2000);
  acknowledge();
  tick(2001);
  tick(4000);
  assert(pending.front().isDiagnostic && outstandingMessageId != 0);
  const std::string inFlight = pending.front().value;
  tick(34000);
  assert(acknowledgementTimeouts == 1);
  assert(client == nullptr);
  assert(pending.front().value == inFlight);
  tick(34001);
  event(MQTT_EVENT_CONNECTED);
  tick(34002);
  assert(sent.back().value == inFlight);
  assert(captureDiagnostic("recovery", true, 34002, diagnosticSnapshot()));
  assert(pending.front().value == inFlight);
  assert(pendingDiagnostic.find("timeout=1") != std::string::npos);
  assert(captureDiagnostic("periodic", true, 34002, diagnosticSnapshot()));
  assert(diagnosticCoalesced == 1);
  acknowledge();
  tick(34003);
  drain();
  assert(publishedValues.empty());

  reset();
  pending.clear();
  pendingLogs.clear();
  std::vector<FeedSample> fullGroup(63, {"weather.temp", "new"});
  for (size_t i = 0; i < fullGroup.size(); ++i) {
    fullGroup[i].value = i % 2 ? "alternate" : "new";
  }
  assert(enqueueGroup("full", fullGroup.data(), fullGroup.size()));
  queueLog("fills last reserved slot");
  assert(queueHighWater == 64);
  assert(!enqueueGroup("full", samples, 2));
  assert(rejectedGroups == 1);
  assert(captureDiagnostic("periodic", true, 0, diagnosticSnapshot()));
  updateDiagnostics(true);
  assert(!pendingDiagnostic.empty() && pending.size() == 63);
  for (size_t i = pendingLogs.size(); i < kMaxLogEntries; ++i) {
    queueLog("backlog");
  }
  queueLog("overflow");
  assert(droppedLogs == 1);
  event(MQTT_EVENT_DISCONNECTED);
  tick(kDiagnosticIntervalMs);
  assert(!pendingDiagnostic.empty());
  const size_t queueDuringOutage = pending.size();
  tick(kDiagnosticIntervalMs * 2);
  assert(pending.size() == queueDuringOutage);
  assert(diagnosticCoalesced >= 2);
  assert(pendingDiagnostic.find("mqtt=down") != std::string::npos);
  assert(pendingDiagnostic.find("log_drop=2") != std::string::npos);
  assert(pendingDiagnostic.size() <= 1024);
  event(MQTT_EVENT_CONNECTED);
  tick(kDiagnosticIntervalMs * 2 + 1);
  drain();
  bool deliveredDiagnostic = false;
  for (const auto& publication : sent) {
    if (publication.topic == "test-user/feeds/aggregator.diag") {
      deliveredDiagnostic = true;
      assert(publication.value.size() <= 1024);
    }
  }
  assert(deliveredDiagnostic);
  for (size_t i = 1; i < sent.size(); ++i) {
    assert(sent[i].at - sent[i - 1].at >= 2000);
  }

  reset();
  MqttDiagnostics maximum;
  lastRate.valid = true;
  lastRate.limit = lastRate.active = UINT32_MAX;
  rateFailures = throttleEvents = UINT32_MAX;
  throttlePaused = true;
  throttleAt = UINT32_MAX - 1000;
  maximum.connects = maximum.disconnects = maximum.errors = UINT32_MAX;
  maximum.errorDetails = true;
  maximum.errorType = maximum.espError = maximum.tlsError =
      maximum.socketError = maximum.refusalCode = INT32_MIN;
  maximum.certFlags = -1;
  maximum.lastOutageMs = maximum.longestOutageMs = UINT32_MAX;
  acknowledgedPublishes = acknowledgementTimeouts = enqueueFailures =
      rejectedGroups = droppedLogs = diagnosticCoalesced = UINT32_MAX;
  weatherkit_current::assemblyFailures = UINT32_MAX;
  weatherkit_current::rawOverflow = UINT32_MAX;
  weatherkit_current::invalidPayloads = UINT32_MAX;
  weatherkit_current::changeOverflow = UINT32_MAX;
  for (auto& source : weatherkit_current::sources) {
    source.received = true;
    source.lastReceivedAt = 0;
  }
  assert(captureDiagnostic("periodic", false, UINT32_MAX, maximum));
  assert(pendingDiagnostic.size() <= 1024);
  const std::string previousSummary = pendingDiagnostic;
  const std::string excessiveReason(1024, 'x');
  assert(!captureDiagnostic(excessiveReason.c_str(), false, UINT32_MAX, maximum));
  assert(pendingDiagnostic == previousSummary);
  assert(Serial.output.find("exceeds 1024 bytes") != std::string::npos);

  reset(0, true);
  assert(captureDiagnostic("periodic", true, 0, diagnosticSnapshot()));
  assert(pending.size() == 1 && pending.front().isLog);
  assert(diagnosticCoalesced == 1);
  assert(pendingDiagnostic.find("diag_coalesced=1") != std::string::npos);
  drain();
  assert(sent.size() == 2);

  reset();
  rejectPublish = true;
  tick(2000);
  tick(4000);
  assert(enqueueFailures == 2);
  assert(sent.empty());

  reset(0, true);
  drain();
  testMillis = 60000;
  event(MQTT_EVENT_DISCONNECTED);
  testMillis = 61000;
  details.error_type = MQTT_ERROR_TYPE_TCP_TRANSPORT;
  errorEvent.error_handle = &details;
  callback(nullptr, nullptr, MQTT_EVENT_ERROR, &errorEvent);
  testMillis = 62000;
  event(MQTT_EVENT_CONNECTED);
  // A callback timestamp can be newer than the loop's initial time sample.
  update(true, true, 59999);
  assert(pending.back().isDiagnostic);
  assert(pending.back().value.find(" recovery ") != std::string::npos);
  assert(pending.back().value.find("outage_s current/last/max=0/2/2") != std::string::npos);
  assert(pending.back().value.find("refusal=0 age_s=1") != std::string::npos);

  reset(UINT32_MAX - 1000, true);
  drain();
  tick(UINT32_MAX - 1000 + kDiagnosticIntervalMs - 1);
  assert(sent.size() == 2);
  tick(UINT32_MAX - 1000 + kDiagnosticIntervalMs);
  assert(sent.back().value.find(" periodic ") != std::string::npos);

  // Heap lows log only below 40 KB and after a further 256-byte drop.
  reset();
  Serial.output.clear();
  ESP.minFreeHeap = 40000;
  tick(1000);
  assert(Serial.output.find("Heap: new low") == std::string::npos);
  ESP.minFreeHeap = 30000;
  ESP.freeHeap = 45000;
  ESP.maxAllocHeap = 20000;
  testTask(nullptr);
  assert(https_gate::tryAcquire("OpenWeather"));
  tick(5000);
  assert(Serial.output.find(
      "Heap: new low min=30000 free=45000 largest=20000 up_s=5 https=OpenWeather "
      "aio_rate=") != std::string::npos);
  assert(Serial.output.find(" mqtt=up wk_last=") != std::string::npos);
  https_gate::release();
  Serial.output.clear();
  ESP.minFreeHeap = 29745;
  tick(6000);
  assert(Serial.output.find("Heap: new low") == std::string::npos);
  ESP.minFreeHeap = 29744;
  tick(7000);
  assert(Serial.output.find("Heap: new low min=29744") != std::string::npos);
  assert(Serial.output.find("https=-") != std::string::npos);
  ESP.minFreeHeap = 180000;
  ESP.freeHeap = 200000;
  ESP.maxAllocHeap = 110000;

  puts("AIO publisher tests passed.");
}
