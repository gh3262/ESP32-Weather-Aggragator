#include "../src/temperature_average.cpp"

#include <cassert>
#include <string>
#include <vector>

TestSerial Serial;
uint32_t testMillis = 0;

namespace {
std::vector<std::string> queued;
unsigned attempts = 0;
bool acceptGroup = true;

void reset() {
  temperature_average::begin();
  queued.clear();
  attempts = 0;
  acceptGroup = true;
  Serial.output.clear();
}
}

namespace aio_publisher {
bool enqueueGroup(const char* name, const FeedSample* samples, size_t count) {
  assert(std::string(name) == "Feed-Avg");
  assert(count == 1 && std::string(samples[0].key) == "feed-avg");
  ++attempts;
  if (!acceptGroup) {
    return false;
  }
  queued.push_back(samples[0].value);
  return true;
}
}

int main() {
  using namespace temperature_average;
  reset();
  update(0);
  assert(attempts == 0);
  observe("unrelated", "not a temperature", 0);
  update(0);
  assert(attempts == 0 && Serial.output.empty());

  observe("om-temp", "60", 0);
  update(0);
  assert(queued.back() == "60");
  observe("currenttemperature", "70.0", 0);
  observe("openweather.owtemp", "80.0", 0);
  observe("wapi.temperature", "90", 0);
  update(0);
  assert(queued.back() == "75");
  for (uint32_t now = 1; now < 1000; ++now) {
    update(now);
  }
  assert(attempts == 2);

  // An unchanged reading renews freshness, without generating a new write.
  observe("om-temp", "60", 1000);
  update(kMaxAgeMs - 1);
  assert(attempts == 2);
  update(kMaxAgeMs);
  assert(queued.back() == "60" && attempts == 3);
  update(kMaxAgeMs + 999);
  assert(attempts == 3);
  update(kMaxAgeMs + 1000);
  assert(attempts == 3);
  assert(Serial.output.find("no fresh temperatures") != std::string::npos);
  const std::string reported = Serial.output;
  update(kMaxAgeMs + 1001);
  assert(Serial.output == reported);
  observe("wapi.temperature", "65", kMaxAgeMs + 1001);
  update(kMaxAgeMs + 1001);
  assert(queued.back() == "65");

  reset();
  observe("om-temp", "-20", 0);
  observe("currenttemperature", "0.0", 0);
  observe("openweather.owtemp", "20.0", 0);
  update(0);
  assert(queued.back() == "0");
  observe("wapi.temperature", "1", 0);
  update(0);
  assert(queued.back() == "0.25");
  observe("wapi.temperature", "-0", 0);
  update(0);
  assert(queued.back() == "0");

  reset();
  observe("om-temp", "60", 0);
  update(0);
  for (const char* invalid : {"", "x", "60x", "nan", "inf", "1e400", "1e-400"}) {
    observe("om-temp", invalid, 1000);
  }
  observe("om-temp", nullptr, 1000);
  assert(Serial.output.find("Feed-Avg ERROR: invalid temperature") != std::string::npos);
  update(kMaxAgeMs);
  assert(attempts == 1 && !inputs[0].fresh);

  // A full queue retries the latest mean, not the previously rejected value.
  reset();
  acceptGroup = false;
  observe("om-temp", "60", 0);
  update(0);
  assert(attempts == 1 && queued.empty());
  observe("currenttemperature", "80", 1);
  update(kRetryMs - 1);
  assert(attempts == 1);
  acceptGroup = true;
  update(kRetryMs);
  assert(attempts == 2 && queued.back() == "70");
  update(kRetryMs + 1);
  assert(attempts == 2);

  reset();
  observe("om-temp", "60", 0);
  update(0);
  acceptGroup = false;
  observe("om-temp", "80", 1);
  update(1);
  observe("om-temp", "60", 2);
  update(2);
  assert(!retryPending && attempts == 2);
  assert(queued.size() == 1 && queued.back() == "60");

  reset();
  observe("om-temp", "60", 0);
  acceptGroup = false;
  update(kMaxAgeMs - 1);
  assert(attempts == 1 && retryPending);
  acceptGroup = true;
  update(kMaxAgeMs + kRetryMs);
  assert(attempts == 1 && queued.empty() && !retryPending);

  reset();
  const uint32_t start = UINT32_MAX - 1000;
  observe("om-temp", "60", start);
  update(start);
  update(start + kMaxAgeMs - 1);
  assert(inputs[0].fresh && attempts == 1);
  update(start + kMaxAgeMs);
  assert(!inputs[0].fresh && attempts == 1);
  update(start);
  assert(!inputs[0].fresh && attempts == 1);

  reset();
  observe("om-temp", "60", start);
  acceptGroup = false;
  update(start);
  acceptGroup = true;
  update(start + kRetryMs - 1);
  assert(attempts == 1);
  update(start + kRetryMs);
  assert(attempts == 2 && queued.back() == "60");

  reset();
  observe("currenttemperature", "70", 101);
  update(100);
  assert(queued.back() == "70" && inputs[1].fresh);
  update(101 + kMaxAgeMs - 1);
  assert(inputs[1].fresh);
  update(101 + kMaxAgeMs);
  assert(!inputs[1].fresh);

  reset();
  observe("om-temp", "1", 0);
  observe("currenttemperature", "2", 0);
  observe("wapi.temperature", "2", 0);
  update(0);
  assert(queued.back() == "1.66667");
}
