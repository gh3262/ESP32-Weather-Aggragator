#ifndef TEST_OPENMETEO_WIFI_CLIENT_SECURE_H
#define TEST_OPENMETEO_WIFI_CLIENT_SECURE_H

#include <cassert>
#include <cstring>

struct WiFiClientSecure {
  bool trusted = false;
  unsigned handshakeTimeout = 0;

  void setCACert(const char* cert) {
    assert(cert && strstr(cert, "-----BEGIN CERTIFICATE-----"));
    trusted = true;
  }
  void setHandshakeTimeout(unsigned timeout) { handshakeTimeout = timeout; }
};

#endif
