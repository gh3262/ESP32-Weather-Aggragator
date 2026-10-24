#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <time.h>
#include <atomic>
#include "status_pixels.h"
#include "aio_publisher.h"
#include "wifi_retry.h"

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

namespace {
constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kScanDeadlineMs = 20000;
constexpr uint32_t kRetryDelayMs = 15000;
constexpr uint32_t kRefreshMs = 1000;
constexpr uint32_t kNtpTimeoutMs = 15000;
constexpr uint32_t kNtpRetryMs = 60000;
constexpr bool kEnableStatusPixels = true;
constexpr int16_t kMargin = 16;
constexpr int16_t kContentWidth = 288;

Adafruit_ST7789 display(&SPI, D6, D7, -1);

enum class NetworkState { Unconfigured, Scanning, Connecting, Online, Waiting };
NetworkState networkState = NetworkState::Unconfigured;
uint32_t stateStartedAt = 0;
uint32_t lastRefreshAt = 0;
uint32_t attempts = 0;
bool refreshNeeded = true;
bool wifiReady = false;
bool pixelsReady = false;
const char* networkMessage = "Not configured";
size_t selectedNetwork = WIFI_NETWORK_COUNT;
bool timeSynchronized = false;
bool ntpPending = false;
bool ntpStarted = false;
uint32_t ntpStartedAt = 0;
std::atomic<int32_t> scanEventStatus{-1};

wifi_retry::SavedNetwork knownNetworks[WIFI_NETWORK_COUNT];

void logWifiDisconnect(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("Wi-Fi: station disconnected; ESP32 reason=%u (%s)\n",
                  static_cast<unsigned>(reason),
                  WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
  }
}

void logWifiScan(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {
    Serial.printf("Wi-Fi: scan completion event; status=%lu, results=%u\n",
                  static_cast<unsigned long>(info.wifi_scan_done.status),
                  static_cast<unsigned>(info.wifi_scan_done.number));
    scanEventStatus.store(static_cast<int32_t>(info.wifi_scan_done.status));
  }
}

void updateStatusPixel() {
  if (!pixelsReady) {
    return;
  }
  static bool havePreviousState = false;
  static NetworkState previousState = NetworkState::Unconfigured;
  if (havePreviousState && networkState == previousState) {
    return;
  }
  bool success;
  switch (networkState) {
    case NetworkState::Online:
      success = status_pixels::setPixel(0, 0, 255, 0);
      break;
    case NetworkState::Scanning:
    case NetworkState::Connecting:
      success = status_pixels::setPixel(0, 255, 100, 0);
      break;
    default:
      success = status_pixels::setPixel(0, 255, 0, 0);
      break;
  }
  if (success) {
    previousState = networkState;
    havePreviousState = true;
  } else {
    pixelsReady = false;
    Serial.println("ERROR: status pixel disabled after write failure");
  }
}

bool hasConfiguredNetworks() {
  for (size_t i = 0; i < WIFI_NETWORK_COUNT; ++i) {
    if (wifiNetworks[i].ssid && wifiNetworks[i].ssid[0] &&
        wifiNetworks[i].password) {
      return true;
    }
  }
  return false;
}

void startTimeSync(uint32_t now) {
  configTzTime(TZ_STRING, NTP_SERVER_1, NTP_SERVER_2);
  ntpStartedAt = now;
  ntpStarted = true;
  ntpPending = true;
  Serial.println("Time: starting NTP synchronization");
}

void updateTime(uint32_t now) {
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    timeSynchronized = true;
    ntpPending = false;
    refreshNeeded = true;
    Serial.println("Time: NTP synchronized");
  }
  if (networkState != NetworkState::Online) {
    return;
  }
  if (ntpPending && now - ntpStartedAt >= kNtpTimeoutMs) {
    ntpPending = false;
    Serial.println("Time: NTP timed out; next request scheduled 60 seconds "
                   "after the previous request");
  }
  if (!timeSynchronized && !ntpPending &&
      (!ntpStarted || now - ntpStartedAt >= kNtpRetryMs)) {
    startTimeSync(now);
  }
}

const char* wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "Connecting";
    case WL_NO_SSID_AVAIL: return "SSID not found";
    case WL_SCAN_COMPLETED: return "Scan completed";
    case WL_CONNECTED: return "Connected";
    case WL_CONNECT_FAILED: return "Connection failed";
    case WL_CONNECTION_LOST: return "Connection lost";
    case WL_DISCONNECTED: return "Disconnected";
    case WL_NO_SHIELD: return "Wi-Fi unavailable";
    default: return "Unknown Wi-Fi status";
  }
}

void waitForRetry(uint32_t now, const char* message) {
  // Cancel work without deinitializing the radio between discovery cycles.
  if (!WiFi.disconnect(false, false)) {
    Serial.println("ERROR: unable to cancel Wi-Fi connection before retry");
  }
  const esp_err_t scanStop = esp_wifi_scan_stop();
  if (scanStop != ESP_OK) {
    Serial.printf("ERROR: unable to stop Wi-Fi scan before retry: %s\n",
                  esp_err_to_name(scanStop));
  }
  WiFi.scanDelete();
  networkMessage = message;
  networkState = NetworkState::Waiting;
  stateStartedAt = now;
  refreshNeeded = true;
  Serial.printf("Wi-Fi: %s; retrying in 15 seconds\n", message);
}

bool connectCachedNetwork(uint32_t now);

void startScan(uint32_t now) {
  if (!WiFi.mode(WIFI_STA)) {
    waitForRetry(now, "Wi-Fi restart failed");
    return;
  }
  stateStartedAt = now;
  selectedNetwork = WIFI_NETWORK_COUNT;
  networkState = NetworkState::Scanning;
  networkMessage = "Scanning";
  refreshNeeded = true;
  WiFi.scanDelete();
  Serial.println("Wi-Fi: scanning for configured networks");
  scanEventStatus.store(-1);
  const int16_t result = WiFi.scanNetworks(true);
  if (result == WIFI_SCAN_FAILED) {
    Serial.printf("Wi-Fi: scan could not start; status=%d, mode=%d\n",
                  static_cast<int>(WiFi.status()),
                  static_cast<int>(WiFi.getMode()));
    Serial.println("Wi-Fi: scan failed; trying last known saved networks");
    if (!connectCachedNetwork(now)) {
      waitForRetry(now, "Scan failed");
    }
  }
}

void connectStrongestNetwork(uint32_t now, int16_t found) {
  for (wifi_retry::SavedNetwork& network : knownNetworks) {
    network.visible = false;
  }
  for (int16_t i = 0; i < found; ++i) {
    const String ssid = WiFi.SSID(i);
    for (size_t j = 0; j < WIFI_NETWORK_COUNT; ++j) {
      if (!wifiNetworks[j].ssid || !wifiNetworks[j].ssid[0] ||
          !wifiNetworks[j].password || ssid != wifiNetworks[j].ssid) {
        continue;
      }
      knownNetworks[j].observe(WiFi.RSSI(i));
      break;
    }
  }
  WiFi.scanDelete();
  if (!connectCachedNetwork(now)) {
    waitForRetry(now, "No eligible Wi-Fi");
  }
}

bool connectCachedNetwork(uint32_t now) {
  const size_t strongest =
      wifi_retry::selectStrongest(knownNetworks, WIFI_NETWORK_COUNT, now);
  if (strongest == WIFI_NETWORK_COUNT) {
    return false;
  }
  selectedNetwork = strongest;
  const WiFiCredential& credential = wifiNetworks[selectedNetwork];
  WiFi.setMinSecurity(credential.password[0] ? WIFI_AUTH_WPA2_PSK
                                           : WIFI_AUTH_OPEN);
  ++attempts;
  stateStartedAt = now;
  networkState = NetworkState::Connecting;
  networkMessage = "Connecting";
  refreshNeeded = true;
  Serial.printf("Wi-Fi: starting connection attempt %lu; credential #%u, "
                "scan RSSI=%ld dBm; driver selects access point/channel\n",
                static_cast<unsigned long>(attempts),
                static_cast<unsigned>(selectedNetwork + 1),
                static_cast<long>(knownNetworks[strongest].rssi));
  const wl_status_t result =
      WiFi.begin(credential.ssid, credential.password);
  Serial.printf("Wi-Fi: begin returned status=%d (%s)\n",
                static_cast<int>(result), wifiStatusText(result));
  return true;
}

void updateNetwork(uint32_t now) {
  if (networkState == NetworkState::Unconfigured) {
    return;
  }
  if (networkState == NetworkState::Scanning) {
    const int32_t eventStatus = scanEventStatus.load();
    if (eventStatus == -1 && now - stateStartedAt < kScanDeadlineMs) {
      return;
    }
    // Read results only after completion to avoid the wrapper's six-second
    // polling timeout aborting valid scans that take longer.
    const int16_t found = eventStatus == 0 ? WiFi.scanComplete() : WIFI_SCAN_FAILED;
    if (found >= 0) {
      Serial.printf("Wi-Fi: scan found %d access points in %lu ms\n",
                    static_cast<int>(found),
                    static_cast<unsigned long>(now - stateStartedAt));
      connectStrongestNetwork(now, found);
    } else if (found == WIFI_SCAN_FAILED) {
      Serial.printf("Wi-Fi: scan failed after %lu ms; status=%d, event_status=%ld\n",
                    static_cast<unsigned long>(now - stateStartedAt),
                    static_cast<int>(WiFi.status()), static_cast<long>(eventStatus));
      const esp_err_t scanStop = esp_wifi_scan_stop();
      if (scanStop != ESP_OK) {
        Serial.printf("ERROR: unable to cancel timed-out Wi-Fi scan: %s\n",
                      esp_err_to_name(scanStop));
      }
      WiFi.scanDelete();
      Serial.println("Wi-Fi: scan failed; trying last known saved networks");
      if (!connectCachedNetwork(now)) {
        waitForRetry(now, "Scan failed");
      }
    }
    return;
  }

  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (networkState != NetworkState::Online) {
      knownNetworks[selectedNetwork].retry.reset();
      networkState = NetworkState::Online;
      networkMessage = "Connected";
      refreshNeeded = true;
      Serial.printf("Wi-Fi: connected; IP %s; RSSI %ld dBm; channel=%ld\n",
                    WiFi.localIP().toString().c_str(),
                    static_cast<long>(WiFi.RSSI()), static_cast<long>(WiFi.channel()));
      startTimeSync(now);
    }
    return;
  }

  if (networkState == NetworkState::Online) {
    waitForRetry(now, wifiStatusText(status));
  } else if (networkState == NetworkState::Connecting &&
             now - stateStartedAt >= kConnectTimeoutMs) {
    networkMessage = status == WL_IDLE_STATUS || status == WL_DISCONNECTED
                         ? "Connection timed out"
                         : wifiStatusText(status);
    Serial.printf("Wi-Fi: connection timed out with status=%d (%s)\n",
                  static_cast<int>(status), wifiStatusText(status));
    wifi_retry::NetworkRetry& retry = knownNetworks[selectedNetwork].retry;
    retry.failed(now);
    Serial.printf("Wi-Fi: credential #%u saved-network failure %u/%u%s\n",
                  static_cast<unsigned>(selectedNetwork + 1),
                  retry.failures, wifi_retry::kFailureThreshold,
                  retry.coolingDown ? "; cooling down for 10 minutes" : "");
    waitForRetry(now, networkMessage);
  } else if (networkState == NetworkState::Waiting &&
             now - stateStartedAt >= kRetryDelayMs) {
    startScan(now);
  }
}

void drawLine(int16_t y, const char* text, uint16_t color = ST77XX_WHITE,
              uint8_t size = 1) {
  display.fillRect(kMargin, y, kContentWidth, 8 * size, ST77XX_BLACK);
  display.setCursor(kMargin, y);
  display.setTextSize(size);
  display.setTextColor(color);
  display.print(text);
}

void drawStatus(uint32_t now) {
  char line[64];
  const bool online = networkState == NetworkState::Online;
  const uint16_t color = online ? ST77XX_GREEN : ST77XX_YELLOW;
  drawLine(44, networkMessage, color, 2);

  if (networkState == NetworkState::Unconfigured) {
    drawLine(70, wifiReady ? "Set networks in include/credentials.h"
                          : "Wi-Fi setup failed; see USB serial.");
    drawLine(86, wifiReady ? "Copy credentials_template.h, then rebuild."
                          : "Reset the board to try initialization again.");
    drawLine(102, "Weather collection waits for Wi-Fi.");
  } else {
    // Limit the byte count so even a full-length SSID fits on one line.
    const String ssid = online ? WiFi.SSID()
                              : selectedNetwork < WIFI_NETWORK_COUNT
                                    ? String(wifiNetworks[selectedNetwork].ssid)
                                    : String("(not selected)");
    snprintf(line, sizeof(line), "SSID: %.32s", ssid.c_str());
    drawLine(70, line);
    if (online) {
      snprintf(line, sizeof(line), "IP: %s   RSSI: %ld dBm",
               WiFi.localIP().toString().c_str(),
               static_cast<long>(WiFi.RSSI()));
    } else if (networkState == NetworkState::Scanning) {
      snprintf(line, sizeof(line), "Scanning for saved networks...");
    } else if (networkState == NetworkState::Connecting) {
      snprintf(line, sizeof(line), "Attempt %lu: %lu / 20 seconds",
               static_cast<unsigned long>(attempts),
               static_cast<unsigned long>((now - stateStartedAt) / 1000));
    } else {
      const uint32_t elapsed = now - stateStartedAt;
      const uint32_t remaining = elapsed < kRetryDelayMs
                                     ? (kRetryDelayMs - elapsed + 999) / 1000
                                     : 0;
      snprintf(line, sizeof(line), "Retry in %lu seconds",
               static_cast<unsigned long>(remaining));
    }
    drawLine(86, line);
    if (timeSynchronized) {
      const time_t currentTime = time(nullptr);
      struct tm localTime;
      if (localtime_r(&currentTime, &localTime) &&
          strftime(line, sizeof(line), "%m/%d/%Y %H:%M:%S %Z",
                   &localTime) != 0) {
        drawLine(102, line);
      } else {
        drawLine(102, "Time formatting failed", ST77XX_YELLOW);
      }
    } else {
      drawLine(102, online ? (ntpPending ? "Time: synchronizing NTP..."
                                       : "Time: NTP timeout; will retry")
                           : "Time: waiting for Wi-Fi");
    }
  }

  snprintf(line, sizeof(line), "Uptime: %lus   Free heap: %lu KB",
           static_cast<unsigned long>(now / 1000),
           static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
  drawLine(122, line);
}
void handleSerialCommands() {
  static uint32_t testRun = 0;
  while (Serial.available()) {
    const int command = Serial.read();
    if (command == 't' || command == 'T') {
      char first[48];
      char second[48];
      snprintf(first, sizeof(first), "publisher-test:%lu:A",
               static_cast<unsigned long>(testRun + 1));
      snprintf(second, sizeof(second), "publisher-test:%lu:B",
               static_cast<unsigned long>(testRun + 1));
      const aio_publisher::FeedSample samples[] = {
          {"aggregator.diag", first}, {"aggregator.diag", first},
          {"aggregator.diag", second}, {"aggregator.diag", first}};
      if (aio_publisher::enqueueGroup("publisher-test", samples, 4)) {
        ++testRun;
        Serial.printf("Publisher test %lu accepted: A,A,B,A submitted; "
                      "expect A,B,A in aggregator.diag, at least 2 seconds apart.\n",
                      static_cast<unsigned long>(testRun));
        Serial.println("Delivery waits for MQTT; aggregator.log will report "
                       "'Sent feed group publisher-test' after acknowledgement.");
      } else {
        Serial.println("ERROR: publisher test rejected; resolve the AIO/queue "
                       "error above, then send t to retry.");
      }
    } else if (command == '?') {
      Serial.println("Commands: t = publisher A/A/B/A test to aggregator.diag, "
                     "? = help.");
    } else if (command != '\r' && command != '\n' && command != ' ') {
      Serial.println("Unknown command; send ? for help.");
    }
  }
}
}  // namespace

void setup() {
  // USB CDC leaves the TX/RX header pins available for the display.
  Serial.begin(115200);
  Serial.println("\nWeather aggregator: hardware and Wi-Fi bring-up");
  Serial.println("Send t for the publisher test to aggregator.diag, ? for help.");
  aio_publisher::begin();
  if (kEnableStatusPixels) {
    pixelsReady = status_pixels::begin();
  } else {
    Serial.println("Diagnostic: NeoPixel initialization and updates disabled");
  }

  pinMode(A3, OUTPUT);
  digitalWrite(A3, HIGH);  // Deselect the unused SD card on the shared SPI bus.
  SPI.begin(SCK, MISO, MOSI, D6);
  display.init(172, 320);
  display.setRotation(3);
  display.setTextWrap(false);
  display.fillScreen(ST77XX_BLACK);
  drawLine(12, "Weather Aggregator", ST77XX_CYAN, 2);
  drawLine(32, "XIAO ESP32-S3  |  ST7789 320x172");
  display.fillRect(16, 146, 96, 8, ST77XX_RED);
  display.fillRect(112, 146, 96, 8, ST77XX_GREEN);
  display.fillRect(208, 146, 96, 8, ST77XX_BLUE);
  Serial.println("Display initialized: SPI SCK=7 MOSI=9 MISO=8 CS=43 DC=44");
  Serial.printf("Free heap: %lu bytes; PSRAM: %lu bytes\n",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getPsramSize()));

  WiFi.onEvent(logWifiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(logWifiScan, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.persistent(false);
  wifiReady = WiFi.mode(WIFI_STA);
  if (!wifiReady) {
    networkMessage = "Wi-Fi init failed";
    Serial.println("ERROR: unable to initialize Wi-Fi station mode");
  } else if (!WiFi.setAutoReconnect(false)) {
    wifiReady = false;
    networkMessage = "Wi-Fi setup failed";
    Serial.println("ERROR: unable to disable automatic Wi-Fi reconnection");
  } else if (!hasConfiguredNetworks()) {
    Serial.println("Wi-Fi not configured: copy include/credentials_template.h "
                   "to include/credentials.h and enter networks, then rebuild.");
  } else {
    startScan(millis());
  }
  lastRefreshAt = millis();
  updateStatusPixel();
  drawStatus(lastRefreshAt);
  refreshNeeded = false;
}

void loop() {
  const uint32_t now = millis();
  updateNetwork(now);
  updateTime(now);
  aio_publisher::update(networkState == NetworkState::Online,
                        timeSynchronized, now);
  handleSerialCommands();
  updateStatusPixel();
  if (refreshNeeded || now - lastRefreshAt >= kRefreshMs) {
    drawStatus(now);
    lastRefreshAt = now;
    refreshNeeded = false;
  }
  delay(10);
}