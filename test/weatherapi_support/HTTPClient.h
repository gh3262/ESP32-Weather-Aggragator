#ifndef TEST_WEATHERAPI_HTTP_CLIENT_H
#define TEST_WEATHERAPI_HTTP_CLIENT_H

#include <cassert>
#include <string>

#include "WiFiClientSecure.h"

constexpr int HTTP_CODE_OK = 200;
extern int testHttpStatus;
extern std::string testHttpBody;
extern unsigned testHttpCalls;

struct HTTPClient {
  int connectTimeout = 0;
  int readTimeout = 0;

  void setConnectTimeout(int timeout) { connectTimeout = timeout; }
  void setTimeout(int timeout) { readTimeout = timeout; }

  bool begin(WiFiClientSecure&, const char* url) {
    assert(std::string(url) ==
           "https://api.weatherapi.com/v1/current.json?key=test-weather-key&q=47.6,-122.3&aqi=yes");
    return true;
  }

  int GET() {
    assert(connectTimeout == 4000 && readTimeout == 4000);
    ++testHttpCalls;
    return testHttpStatus;
  }

  int writeToStream(Stream* stream) {
    return static_cast<int>(stream->write(
        reinterpret_cast<const uint8_t*>(testHttpBody.data()),
        testHttpBody.size()));
  }

  void end() {}
};

#endif
