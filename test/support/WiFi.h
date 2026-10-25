#ifndef TEST_WIFI_H
#define TEST_WIFI_H

struct TestWiFi {
  int RSSI() const { return -55; }
};
extern TestWiFi WiFi;

#endif
