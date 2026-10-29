#ifndef TEST_NWS_HTTP_CLIENT_H
#define TEST_NWS_HTTP_CLIENT_H

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
           "https://api.weather.gov/alerts/active?point=24.577389,-81.766889");
    return true;
  }

  void addHeader(const char* key, const char* value) {
    if (std::string(key) == "User-Agent") {
      assert(std::string(value) ==
             "ESP32-Weather-Aggregator/1.0 (NWS alerts)");
    } else {
      assert(std::string(key) == "Accept");
      assert(std::string(value) == "application/geo+json");
    }
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
