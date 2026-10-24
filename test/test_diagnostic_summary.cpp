#include "diagnostic_summary.h"

#include <assert.h>
#include <string.h>

int main() {
  const diagnostic_summary::Status status = {
      "[2026-10-24 10:30:00 CDT | up 1h00m00s]", true, 1, 0, 0, -51,
      220744, 214220};
  diagnostic_summary::Topic topics[] = {
      {"cur", 60, 5, 0, 728, 61, 2},
      {"min5", 1, 0, 0, 91, 0, 3},
      {"day5", 0, 0, 0, 0, 0, 0}};
  char output[diagnostic_summary::kMaxBytes + 1];
  assert(diagnostic_summary::format(output, status, topics, 3));
  assert(strstr(output, "MQTT=up c/d/e=1/0/0 RSSI=-51") != nullptr);
  assert(strstr(output, "cur: 60,5,0,728,61,2\n") != nullptr);
  assert(strstr(output, "min5: 1,0,0,91,-,3\n") != nullptr);
  assert(strstr(output, "day5: 0,0,0,0,-,-\n") != nullptr);

  diagnostic_summary::Topic worstTopics[10];
  for (auto& topic : worstTopics) {
    topic = {"min30", UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
             UINT32_MAX, UINT32_MAX};
  }
  const diagnostic_summary::Status worst = {
      "[2026-10-24 10:30:00 ABCDEFGHIJKLMNOP | up 999999h59m59s]",
      false, UINT32_MAX, UINT32_MAX, UINT32_MAX, INT32_MIN, UINT32_MAX,
      UINT32_MAX};
  assert(diagnostic_summary::format(output, worst, worstTopics, 10));
  assert(strlen(output) <= diagnostic_summary::kMaxBytes);

  diagnostic_summary::Topic oversized[20];
  for (auto& topic : oversized) {
    topic = worstTopics[0];
  }
  assert(!diagnostic_summary::format(output, worst, oversized, 20));
  puts("Diagnostic summary tests passed.");
}
