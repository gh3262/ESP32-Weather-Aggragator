#define CREDENTIALS_H
#define LATITUDE "35.1548152"
#define LONGITUDE "-89.7277686"
#define NWS_ALERTS_LATITUDE "24.577389"
#define NWS_ALERTS_LONGITUDE "-81.766889"
#include "../src/nws_alerts.cpp"

#include <cassert>
#include <map>
#include <string>

TestSerial Serial;
uint32_t testMillis = 0;
TestEsp ESP;
TestWiFi WiFi;
void (*testTask)(void*) = nullptr;
bool testTaskFailure = false;
int testHttpStatus = 200;
std::string testHttpBody;
unsigned testHttpCalls = 0;

namespace {
std::map<std::string, std::string> accepted;
unsigned submissions = 0;
bool acceptGroup = true;

const char activeAlerts[] = R"JSON({
  "type":"FeatureCollection",
  "features":[
    {"properties":{
      "sent":"2026-10-29T12:00:00-04:00",
      "severity":"Moderate",
      "urgency":"Expected",
      "event":"Rip Current Statement",
      "senderName":"NWS Key West FL",
      "headline":"Life-threatening rip currents are expected.",
      "description":"Entering the surf is discouraged.",
      "parameters":{"NWSheadline":["Rip Current Statement remains in effect."]}
    }},
    {"properties":{
      "sent":"2026-10-29T12:05:00-04:00",
      "severity":"Minor",
      "urgency":"Expected",
      "event":"Small Craft Advisory",
      "senderName":"NWS Key West FL",
      "headline":"Winds will increase this afternoon.",
      "description":"Small craft should exercise caution.",
      "parameters":{"NWSheadline":["Small Craft Advisory in effect."]}
    }}
  ]
})JSON";

const char noAlerts[] = R"JSON({"type":"FeatureCollection","features":[]})JSON";

std::string sixAlertsWithLongDetails() {
  std::string body = "{\"type\":\"FeatureCollection\",\"features\":[";
  for (size_t index = 0; index < 6; ++index) {
    if (index) {
      body += ",";
    }
    body += "{\"properties\":{\"event\":\"Event ";
    body += std::to_string(index + 1);
    body += "\",\"headline\":\"Headline ";
    body += std::to_string(index + 1);
    body += "\",\"description\":\"";
    body.append(700, 'X');
    body += "\",\"parameters\":{\"NWSheadline\":[\"NWS headline\"]}}}";
  }
  body += "]}";
  return body;
}

void reset() {
  using namespace nws_alerts;
  workerResult = FetchResult{};
  workerRunning = false;
  resultPending = false;
  configured = false;
  activeAlertLevel = AlertLevel::None;
  attempted = false;
  lastAttemptAt = nextAttemptDelay = 0;
  alerts.clear();
  pendingSamples.clear();
  nextAlertIndex = pendingAlertIndex = 0;
  pendingIsAlert = false;
  snapshotPending = false;
  pendingSince = lastCycleAt = lastAcceptedAt = 0;
  haveAccepted = false;
  publishRetryPending = false;
  lastPublishRetryAt = failedRequests = 0;
  https_gate::release();
  testTask = nullptr;
  testTaskFailure = false;
  testHttpStatus = HTTP_CODE_OK;
  testHttpBody = activeAlerts;
  testHttpCalls = 0;
  testMillis = 0;
  accepted.clear();
  submissions = 0;
  acceptGroup = true;
  Serial.output.clear();
}

const std::string& value(const char* key) {
  const auto found = accepted.find(key);
  assert(found != accepted.end());
  return found->second;
}

void completeFetch(uint32_t at) {
  using namespace nws_alerts;
  update(true, true, at);
  assert(testTask);
  testTask(nullptr);
  update(true, true, at + 1);
}
}

namespace aio_publisher {
bool enqueueGroup(const char* name, const FeedSample* samples, size_t count) {
  assert(std::string(name) == "NWS Alerts");
  ++submissions;
  if (!acceptGroup) {
    return false;
  }
  accepted.clear();
  for (size_t i = 0; i < count; ++i) {
    assert(accepted.emplace(samples[i].key, samples[i].value).second);
  }
  return true;
}
}

int main() {
  using namespace nws_alerts;
  assert(kPollIntervalMs == 5UL * 60UL * 1000UL);
  assert(kRetryDelayMs == 60UL * 1000UL);
  assert(kRotationIntervalMs == 15UL * 1000UL);

  assert(eventLevel("Winter Storm Warning") == AlertLevel::Warning);
  assert(eventLevel("Tornado Watch") == AlertLevel::Watch);
  assert(eventLevel("Special Weather Statement") == AlertLevel::Statement);
  assert(eventLevel("Winter Weather Advisory") == AlertLevel::Statement);
  reset();
  begin();
  assert(alertPixel(1000).red == 0 && alertPixel(1000).green == 0 &&
         alertPixel(1000).blue == 0);
  activeAlertLevel = AlertLevel::Warning;
  assert(alertPixel(0).red == 0);
  assert(alertPixel(500).red == 127);
  assert(alertPixel(1000).red == 255);
  assert(alertPixel(1500).red == 127);
  assert(alertPixel(2000).red == 0);
  assert(alertPixel(1000).green == 0 && alertPixel(1000).blue == 0);
  activeAlertLevel = AlertLevel::Watch;
  assert(alertPixel(1000).red == 255 && alertPixel(1000).green == 100 &&
         alertPixel(1000).blue == 0);
  activeAlertLevel = AlertLevel::Statement;
  assert(alertPixel(1000).red == 0 && alertPixel(1000).green == 0 &&
         alertPixel(1000).blue == 255);

  assert(withoutWhereSection("") == "");
  const char sectionedDescription[] =
      "* WHAT...Heavy snow.\n\n"
      "* WHERE...Red Dog Mine and surrounding cities,\n"
      "including several additional locations.\n\n"
      "* WHEN...Until 4 PM.\n\n"
      "* IMPACTS...Travel difficult.\n\n"
      "* ADDITIONAL DETAILS...Blowing snow.\n";
  const char compactDescription[] =
      "* WHAT...Heavy snow.\n\n"
      "* WHEN...Until 4 PM.\n\n"
      "* IMPACTS...Travel difficult.\n\n"
      "* ADDITIONAL DETAILS...Blowing snow.\n";
  assert(withoutWhereSection(sectionedDescription) == compactDescription);
  assert(withoutWhereSection("* WHERE...Cities.\nWrapped locations.") == "");
  assert(withoutWhereSection("  * WHERE...Cities.\r\nMore cities.\r\n"
                             "\t* WHEN...Tonight.\r\n") ==
         "\t* WHEN...Tonight.\r\n");
  assert(withoutWhereSection("Where flooding is possible.\n"
                             "* WHAT...A warning mentioning WHERE in prose.") ==
         "Where flooding is possible.\n"
         "* WHAT...A warning mentioning WHERE in prose.");
  assert(withoutWhereSection("* WHERE...Cities.\n* WHERE...More cities.\n"
                             "* WHEN...Tonight.") == "* WHEN...Tonight.");

  reset();
  begin();
  JsonDocument fixture;
  fixture["features"][0]["properties"]["event"] = "Winter Storm Warning";
  fixture["features"][0]["properties"]["description"] =
      std::string("* WHAT...Snow.\n\n* WHERE...") +
      std::string(2000, 'X') +
      "\nWrapped locations.\n\n* WHEN...Tonight.\n"
      "* IMPACTS...Travel difficult.";
  testHttpBody.clear();
  serializeJson(fixture, testHttpBody);
  completeFetch(0);
  const std::string compactBig = value("big-alert");
  assert(compactBig.find("WHERE") == std::string::npos);
  assert(compactBig.find("Wrapped locations") == std::string::npos);
  assert(compactBig.find("* WHAT...Snow.") != std::string::npos);
  assert(compactBig.find("* WHEN...Tonight.") != std::string::npos);
  assert(compactBig.find("* IMPACTS...Travel difficult.") != std::string::npos);
  assert(compactBig.find("[truncated]") == std::string::npos);
  assert(compactBig.size() <= kMaxFeedValueLength);

  reset();
  begin();
  fixture["features"][0]["properties"]["description"] =
      std::string("* WHERE...Cities.\n* WHAT...") + std::string(700, 'X');
  testHttpBody.clear();
  serializeJson(fixture, testHttpBody);
  completeFetch(0);
  assert(value("big-alert").find("WHERE") == std::string::npos);
  assert(value("big-alert").find("* WHAT...") != std::string::npos);
  assert(value("big-alert").size() == kMaxFeedValueLength);
  assert(value("big-alert").find("[truncated]") != std::string::npos);

  reset();
  begin();
  assert(configured);
  assert(health(0) == HealthStatus::Amber);
  update(true, false, 0);
  assert(!testTask && testHttpCalls == 0);

  completeFetch(0);
  assert(testHttpCalls == 1 && submissions == 1);
  assert(accepted.size() == 4);
  assert(value("nbr-alerts") == "2");
  assert(value("weather-alerts").find("ALERT 1 of 2") != std::string::npos);
  assert(value("headline-alert") ==
         "Rip Current Statement remains in effect. - "
         "Life-threatening rip currents are expected.");
  assert(value("big-alert").find("Entering the surf is discouraged.") !=
         std::string::npos);
  assert(value("big-alert").size() <= kMaxFeedValueLength);
  assert(accepted.count("rt") == 0);
  assert(accepted.count("lat") == 0 && accepted.count("lon") == 0);
  assert(health(1) == HealthStatus::Green);
  assert(activeAlertLevel == AlertLevel::Statement);
  assert(Serial.output.find("ESP32-Weather-Aggregator") ==
         std::string::npos);

  update(true, true, kRotationIntervalMs);
  assert(submissions == 1);
  update(true, true, kRotationIntervalMs + 1);
  assert(submissions == 2);
  assert(value("weather-alerts").find("ALERT 2 of 2") != std::string::npos);
  assert(value("headline-alert") ==
         "Small Craft Advisory in effect. - "
         "Winds will increase this afternoon.");

  // A queue rejection retains the whole selected alert for retry.
  acceptGroup = false;
  update(true, true, 2 * kRotationIntervalMs + 1);
  assert(!pendingSamples.empty());
  const size_t retainedCount = pendingSamples.size();
  const uint32_t retainedSince = pendingSince;
  update(true, true, 2 * kRotationIntervalMs + 2);
  assert(submissions == 3 && pendingSince == retainedSince);
  acceptGroup = true;
  update(true, true, 2 * kRotationIntervalMs + 1 + kPublishRetryMs);
  assert(pendingSamples.empty());
  assert(submissions == 4);
  assert(retainedCount == 4);
  assert(value("weather-alerts").find("ALERT 1 of 2") != std::string::npos);

  // A successful empty collection publishes the no-alert state and count.
  reset();
  begin();
  testHttpBody = noAlerts;
  completeFetch(0);
  assert(accepted.size() == 4);
  assert(value("nbr-alerts") == "0");
  assert(value("weather-alerts") == "No Current Alerts");
  assert(value("headline-alert") == "No Current Alerts");
  assert(value("big-alert") == "No Current Alerts");
  assert(activeAlertLevel == AlertLevel::None);

  // The endpoint has no supported limit parameter; only five are retained.
  reset();
  begin();
  testHttpBody = sixAlertsWithLongDetails();
  completeFetch(0);
  assert(value("nbr-alerts") == "5");
  assert(value("weather-alerts").find("ALERT 1 of 5") != std::string::npos);
  assert(value("big-alert").size() == kMaxFeedValueLength);
  assert(value("big-alert").find("[truncated]") != std::string::npos);

  // A warning beyond the five published alerts still controls the indicator.
  reset();
  begin();
  testHttpBody = sixAlertsWithLongDetails();
  testHttpBody.replace(testHttpBody.find("Event 6"), 7, "Winter Storm Warning");
  completeFetch(0);
  assert(alerts.size() == 5 && activeAlertLevel == AlertLevel::Warning);
  testHttpStatus = 503;
  completeFetch(kPollIntervalMs);
  assert(activeAlertLevel == AlertLevel::Warning);
  testHttpStatus = HTTP_CODE_OK;
  testHttpBody = noAlerts;
  completeFetch(kPollIntervalMs + 1 + kRetryDelayMs);
  assert(activeAlertLevel == AlertLevel::None);

  reset();
  begin();
  testHttpBody = activeAlerts;
  testHttpBody.replace(testHttpBody.find("Small Craft Advisory"), 20,
                       "Winter Storm Watch");
  completeFetch(0);
  assert(activeAlertLevel == AlertLevel::Watch);

  // Oversized responses fail explicitly and do not clear accepted values.
  reset();
  begin();
  testHttpBody.assign(kMaxResponseBytes + 1, 'x');
  completeFetch(0);
  assert(submissions == 0);
  assert(failedRequests == 1);
  assert(health(1) == HealthStatus::Amber);
  assert(std::string(https_gate::owner()) == "-");

  // The shared HTTPS slot defers this provider without counting an attempt.
  reset();
  begin();
  assert(https_gate::tryAcquire("other"));
  update(true, true, 0);
  assert(!testTask && testHttpCalls == 0);
  assert(std::string(https_gate::owner()) == "other");
  https_gate::release();
  update(true, true, 1);
  assert(testTask);
  testTask(nullptr);
  update(true, true, 2);
  assert(testHttpCalls == 1);

  // Repeated request failures escalate health and preserve prior alerts.
  reset();
  begin();
  testHttpStatus = 503;
  update(true, true, 0);
  assert(testTask);
  testTask(nullptr);
  update(true, true, 1);
  assert(failedRequests == 1);
  assert(health(1) == HealthStatus::Amber);
  assert(testHttpCalls == 1);
  update(true, true, kRetryDelayMs + 1);
  assert(testTask);
  testTask(nullptr);
  update(true, true, kRetryDelayMs + 2);
  assert(failedRequests == 2);
  update(true, true, 2 * kRetryDelayMs + 2);
  assert(testTask);
  testTask(nullptr);
  update(true, true, 2 * kRetryDelayMs + 3);
  assert(failedRequests == 3);
  assert(health(2 * kRetryDelayMs + 3) == HealthStatus::Red);
  assert(std::string(https_gate::owner()) == "-");
}
