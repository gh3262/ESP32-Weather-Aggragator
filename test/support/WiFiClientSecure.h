#ifndef TEST_WIFI_CLIENT_SECURE_H
#define TEST_WIFI_CLIENT_SECURE_H
struct WiFiClientSecure {
  static unsigned& liveInstances() { static unsigned count = 0; return count; }
  WiFiClientSecure() { ++liveInstances(); }
  ~WiFiClientSecure() { --liveInstances(); }
  void setCACert(const char*) {}
  void setHandshakeTimeout(unsigned) {}
};
#endif
