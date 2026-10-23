#include "wifi_retry.h"
#include <cstdio>

int main() {
  unsigned failures = 0;
  unsigned checks = 0;
  auto check = [&](bool condition) {
    ++checks;
    if (!condition) {
      ++failures;
      std::fprintf(stderr, "FAIL: retry check %u\n", checks);
    }
  };
  wifi_retry::NetworkRetry first;
  wifi_retry::NetworkRetry second;
  check(first.eligible(0));
  first.failed(10);
  check(first.failures == 1 && first.eligible(10));
  first.failed(20);
  check(first.failures == 2 && !first.eligible(20));
  check(second.eligible(20));
  check(!first.eligible(20 + wifi_retry::kCooldownMs - 1));
  check(first.eligible(20 + wifi_retry::kCooldownMs));
  check(first.failures == 0);
  first.failed(30);
  first.reset();
  check(first.failures == 0 && first.eligible(30));
  const uint32_t nearWrap = UINT32_MAX - 100;
  first.failed(nearWrap);
  first.failed(nearWrap);
  check(!first.eligible(nearWrap + wifi_retry::kCooldownMs - 1));
  check(first.eligible(nearWrap + wifi_retry::kCooldownMs));
  wifi_retry::SavedNetwork networks[3];
  check(wifi_retry::selectStrongest(networks, 3, 0) == 3);
  networks[0].observe(-60);
  networks[0].observe(-30);
  networks[0].observe(-50);
  check(networks[0].rssi == -30);
  networks[1].observe(-40);
  networks[2].observe(-70);
  check(wifi_retry::selectStrongest(networks, 3, 0) == 0);
  networks[0].retry.failed(100);
  check(wifi_retry::selectStrongest(networks, 3, 100) == 0);
  networks[0].retry.failed(200);
  check(wifi_retry::selectStrongest(networks, 3, 200) == 1);
  networks[0].observe(-20);
  check(wifi_retry::selectStrongest(networks, 3, 200) == 1);
  networks[1].retry.failed(200);
  networks[1].retry.failed(200);
  check(wifi_retry::selectStrongest(networks, 3, 200) == 2);
  networks[2].retry.failed(200);
  networks[2].retry.failed(200);
  check(wifi_retry::selectStrongest(networks, 3, 200) == 3);
  check(wifi_retry::selectStrongest(networks, 3, 200 + wifi_retry::kCooldownMs) == 0);
  networks[0].visible = false;
  networks[0].observe(-80);
  check(networks[0].rssi == -80);
  check(wifi_retry::selectStrongest(networks, 3, 200 + wifi_retry::kCooldownMs) == 1);
  std::printf("%u checks, %u failures\n", checks, failures);
  return failures ? 1 : 0;
}
