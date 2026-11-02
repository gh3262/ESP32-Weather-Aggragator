#pragma once

#include <cassert>
#include <string>
#include "..\openmeteo_support\WiFiClientSecure.h"

constexpr int HTTP_CODE_OK = 200;
extern int testHttpStatus;
extern std::string testHttpBody;
extern unsigned testHttpCalls;
extern bool testHttpReadFailure;
extern std::string testUrl;

struct HTTPClient {
  void setConnectTimeout(int timeout) { assert(timeout == 4000); }
  void setTimeout(int timeout) { assert(timeout == 4000); }
  bool begin(WiFiClientSecure& transport, const char* url) {
    assert(transport.trusted && transport.handshakeTimeout == 5);
    testUrl = url;
    return true;
  }
  int GET() { ++testHttpCalls; return testHttpStatus; }
  int writeToStream(Stream* body) {
    if (testHttpReadFailure) return -1;
    body->write(reinterpret_cast<const uint8_t*>(testHttpBody.data()), testHttpBody.size());
    return static_cast<int>(testHttpBody.size());
  }
  void end() {}
};
