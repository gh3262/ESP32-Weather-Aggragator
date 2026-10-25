#ifndef TEST_WIFI_CLIENT_SECURE_H
#define TEST_WIFI_CLIENT_SECURE_H
struct WiFiClientSecure {
  void setCACert(const char*) {}
  void setHandshakeTimeout(unsigned) {}
};
#endif
