#ifndef TEST_NWS_FORECAST_HTTP_CLIENT_H
#define TEST_NWS_FORECAST_HTTP_CLIENT_H

#include <cassert>
#include <string>
#include "WiFiClientSecure.h"

constexpr int HTTP_CODE_OK = 200;
extern int testHttpStatus;
extern std::string testHttpBody;
extern std::string testHttpUrl;
extern unsigned testHttpCalls;
extern bool testReadFailure;

struct HTTPClient {
  int connectTimeout = 0;
  int readTimeout = 0;
  bool agent = false;
  bool accept = false;
  void setConnectTimeout(int timeout) { connectTimeout = timeout; }
  void setTimeout(int timeout) { readTimeout = timeout; }
  bool begin(WiFiClientSecure&, const char* url) {
    testHttpUrl = url;
    return true;
  }
  void addHeader(const char* key, const char* value) {
    if (std::string(key) == "User-Agent") {
      assert(std::string(value) == "ESP32-Weather-Aggregator/1.0 (NWS forecasts)");
      agent = true;
    } else {
      assert(std::string(key) == "Accept" &&
             std::string(value) == "application/geo+json");
      accept = true;
    }
  }
  int GET() {
    assert(agent && accept && connectTimeout == 4000 && readTimeout == 4000);
    ++testHttpCalls;
    return testHttpStatus;
  }
  int writeToStream(Stream* stream) {
    if (testReadFailure) return -1;
    return static_cast<int>(stream->write(
        reinterpret_cast<const uint8_t*>(testHttpBody.data()), testHttpBody.size()));
  }
  void end() {}
};
#endif
