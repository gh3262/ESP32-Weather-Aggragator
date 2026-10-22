#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials_template.h"
#endif

namespace {
constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kRetryDelayMs = 15000;
constexpr uint32_t kRefreshMs = 1000;
constexpr uint32_t kNtpTimeoutMs = 15000;
constexpr uint32_t kNtpRetryMs = 60000;
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
const char* networkMessage = "Not configured";
size_t selectedNetwork = WIFI_NETWORK_COUNT;
bool timeSynchronized = false;
bool ntpPending = false;
bool ntpStarted = false;
uint32_t ntpStartedAt = 0;

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
  networkMessage = message;
  networkState = NetworkState::Waiting;
  stateStartedAt = now;
  refreshNeeded = true;
  Serial.printf("Wi-Fi: %s; rescanning in 15 seconds\n", message);
}

void startScan(uint32_t now) {
  stateStartedAt = now;
  selectedNetwork = WIFI_NETWORK_COUNT;
  networkState = NetworkState::Scanning;
  networkMessage = "Scanning";
  refreshNeeded = true;
  WiFi.scanDelete();
  Serial.println("Wi-Fi: scanning for configured networks");
  const int16_t result = WiFi.scanNetworks(true);
  if (result == WIFI_SCAN_FAILED) {
    waitForRetry(now, "Scan failed");
  }
}

void connectStrongestNetwork(uint32_t now, int16_t found) {
  int16_t strongest = -1;
  int32_t strongestRssi = INT32_MIN;
  for (int16_t i = 0; i < found; ++i) {
    const String ssid = WiFi.SSID(i);
    for (size_t j = 0; j < WIFI_NETWORK_COUNT; ++j) {
      if (!wifiNetworks[j].ssid || !wifiNetworks[j].ssid[0] ||
          !wifiNetworks[j].password || ssid != wifiNetworks[j].ssid) {
        continue;
      }
      if (WiFi.RSSI(i) > strongestRssi && WiFi.BSSID(i)) {
        strongest = i;
        strongestRssi = WiFi.RSSI(i);
        selectedNetwork = j;
      }
      break;
    }
  }
  if (strongest < 0) {
    WiFi.scanDelete();
    waitForRetry(now, "No known Wi-Fi");
    return;
  }

  uint8_t bssid[6];
  memcpy(bssid, WiFi.BSSID(strongest), sizeof(bssid));
  const int32_t channel = WiFi.channel(strongest);
  WiFi.scanDelete();
  const WiFiCredential& credential = wifiNetworks[selectedNetwork];
  WiFi.setMinSecurity(credential.password[0] ? WIFI_AUTH_WPA2_PSK
                                           : WIFI_AUTH_OPEN);
  ++attempts;
  stateStartedAt = now;
  networkState = NetworkState::Connecting;
  networkMessage = "Connecting";
  refreshNeeded = true;
  Serial.printf("Wi-Fi: starting connection attempt %lu\n",
                static_cast<unsigned long>(attempts));
  WiFi.begin(credential.ssid, credential.password, channel, bssid);
}

void updateNetwork(uint32_t now) {
  if (networkState == NetworkState::Unconfigured) {
    return;
  }
  if (networkState == NetworkState::Scanning) {
    const int16_t found = WiFi.scanComplete();
    if (found >= 0) {
      connectStrongestNetwork(now, found);
    } else if (found == WIFI_SCAN_FAILED) {
      WiFi.scanDelete();
      waitForRetry(now, "Scan failed");
    }
    return;
  }

  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (networkState != NetworkState::Online) {
      networkState = NetworkState::Online;
      networkMessage = "Connected";
      refreshNeeded = true;
      Serial.printf("Wi-Fi: connected; IP %s; RSSI %ld dBm\n",
                    WiFi.localIP().toString().c_str(),
                    static_cast<long>(WiFi.RSSI()));
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
    if (!WiFi.disconnect()) {
      Serial.println("Wi-Fi: failed to stop the connection attempt");
    }
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
    drawLine(102, "Weather feeds are not enabled yet.");
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
}  // namespace

void setup() {
  // USB CDC leaves the TX/RX header pins available for the display.
  Serial.begin(115200);
  Serial.println("\nWeather aggregator: hardware and Wi-Fi bring-up");

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
  drawStatus(lastRefreshAt);
  refreshNeeded = false;
}

void loop() {
  const uint32_t now = millis();
  updateNetwork(now);
  updateTime(now);
  if (refreshNeeded || now - lastRefreshAt >= kRefreshMs) {
    drawStatus(now);
    lastRefreshAt = now;
    refreshNeeded = false;
  }
  delay(10);
}