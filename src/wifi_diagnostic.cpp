#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

namespace {
constexpr uint32_t kTimeoutMs = 30000;
constexpr uint32_t kStartupWaitMs = 10000;
constexpr uint32_t kScanDeadlineMs = 20000;
std::atomic<int32_t> scanEventStatus{-1};
size_t selected = WIFI_NETWORK_COUNT > 2 ? 2 : 0;
uint32_t attemptStartedAt = 0;
bool attempting = false;
bool wasConnected = false;

void printHelp() {
  Serial.println("\n=== ISOLATED WI-FI DIAGNOSTIC ===");
  Serial.println("No display, NeoPixel, BSSID/channel binding, "
                 "NTP, or fallback.");
  Serial.printf("Configured entries: %u; selected: #%u\n",
                static_cast<unsigned>(WIFI_NETWORK_COUNT),
                static_cast<unsigned>(selected + 1));
  Serial.println("Send 1-9 to select and connect; r retries; ? repeats this help.");
  Serial.println("Send s for five scan-only tests (disconnects current network).");
  Serial.println("Passwords are never logged. Only one console may own USB serial.");
}

void logDisconnect(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("Wi-Fi disconnect: %u (%s)\n", static_cast<unsigned>(reason),
                  WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
  }
}

void logScan(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {
    Serial.printf("SCAN EVENT: status=%lu, results=%u\n",
                  static_cast<unsigned long>(info.wifi_scan_done.status),
                  static_cast<unsigned>(info.wifi_scan_done.number));
    scanEventStatus.store(static_cast<int32_t>(info.wifi_scan_done.status));
  }
}

void runScanTests() {
  attempting = false;
  wasConnected = false;
  if (!WiFi.mode(WIFI_STA) || !WiFi.setAutoReconnect(false) ||
      !WiFi.disconnect(false, false)) {
    Serial.println("ERROR: unable to prepare scan-only test.");
    return;
  }
  delay(1000);
  Serial.println("SCAN TEST: five scans, 20-second event deadline, "
                 "unchanged default channel dwell.");
  unsigned completed = 0;
  for (unsigned scan = 1; scan <= 5; ++scan) {
    WiFi.scanDelete();
    scanEventStatus.store(-1);
    const uint32_t startedAt = millis();
    int16_t result = WiFi.scanNetworks(true);
    // scanComplete() enforces a six-second polling timeout. Wait for the
    // completion callback first so this test can measure the actual scan time.
    while (result == WIFI_SCAN_RUNNING && scanEventStatus.load() == -1 &&
           millis() - startedAt < kScanDeadlineMs) {
      delay(10);
    }
    const int32_t eventStatus = scanEventStatus.load();
    if (result == WIFI_SCAN_RUNNING && eventStatus == 0) {
      result = WiFi.scanComplete();
    } else if (result == WIFI_SCAN_RUNNING) {
      result = WIFI_SCAN_FAILED;
    }
    Serial.printf("SCAN %u/5: result=%d, event_status=%ld, elapsed=%lu ms\n", scan,
                  static_cast<int>(result),
                  static_cast<long>(eventStatus),
                  static_cast<unsigned long>(millis() - startedAt));
    if (result >= 0) {
      ++completed;
    } else {
      const esp_err_t stopped = esp_wifi_scan_stop();
      Serial.printf("SCAN cancellation: %s\n", esp_err_to_name(stopped));
    }
    // Let completion/cancellation events drain before deleting results.
    delay(2000);
    WiFi.scanDelete();
  }
  Serial.printf("SCAN TEST COMPLETE: %u/5 completed. Send 2 to connect, "
                "then s to test scanning after a connection.\n", completed);
}

void startAttempt(size_t index) {
  if (index >= WIFI_NETWORK_COUNT || !wifiNetworks[index].ssid ||
      !wifiNetworks[index].ssid[0] || !wifiNetworks[index].password) {
    Serial.println("ERROR: selected credential entry is missing or empty.");
    return;
  }
  attempting = false;
  wasConnected = false;
  selected = index;
  if (!WiFi.mode(WIFI_OFF)) {
    Serial.println("ERROR: unable to stop previous Wi-Fi session.");
    return;
  }
  if (!WiFi.mode(WIFI_STA) || !WiFi.setAutoReconnect(false)) {
    Serial.println("ERROR: unable to configure Wi-Fi station.");
    return;
  }
  const WiFiCredential& credential = wifiNetworks[index];
  WiFi.setMinSecurity(credential.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  Serial.printf("Connecting with credential #%u using plain WiFi.begin(); "
                "30-second timeout.\n", static_cast<unsigned>(index + 1));
  attemptStartedAt = millis();
  attempting = true;
  const wl_status_t result = WiFi.begin(credential.ssid, credential.password);
  Serial.printf("WiFi.begin() returned status %d (connection is asynchronous).\n",
                static_cast<int>(result));
}
}  // namespace

void setup() {
  Serial.begin(115200);
  // Give Upload and Monitor time to reopen native USB, without requiring it.
  const uint32_t startedAt = millis();
  while (millis() - startedAt < kStartupWaitMs) {
    delay(10);
  }
  printHelp();
  WiFi.persistent(false);
  WiFi.onEvent(logDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(logScan, ARDUINO_EVENT_WIFI_SCAN_DONE);
  runScanTests();
}

void loop() {
  while (Serial.available()) {
    const int command = Serial.read();
    if (command >= '1' && command <= '9') {
      startAttempt(static_cast<size_t>(command - '1'));
    } else if (command == 'r' || command == 'R') {
      startAttempt(selected);
    } else if (command == '?') {
      printHelp();
    } else if (command == 's' || command == 'S') {
      runScanTests();
    }
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("CONNECTED: credential #%u, IP=%s, RSSI=%ld dBm, channel=%ld\n",
                  static_cast<unsigned>(selected + 1),
                  WiFi.localIP().toString().c_str(),
                  static_cast<long>(WiFi.RSSI()), static_cast<long>(WiFi.channel()));
    attempting = false;
  } else if (!connected && wasConnected) {
    Serial.println("Connection lost. Send r to retry or a credential number.");
  }
  wasConnected = connected;
  if (attempting && millis() - attemptStartedAt >= kTimeoutMs) {
    Serial.printf("TIMEOUT: credential #%u, status=%d. No automatic retry.\n",
                  static_cast<unsigned>(selected + 1), static_cast<int>(WiFi.status()));
    attempting = false;
    if (!WiFi.mode(WIFI_OFF)) {
      Serial.println("ERROR: unable to stop Wi-Fi after timeout.");
    }
    Serial.println("Send r to retry, 1-9 to select another network, or ? for help.");
  }
  delay(10);
}
