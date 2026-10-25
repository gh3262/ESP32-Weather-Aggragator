#ifndef TEST_HTTP_CLIENT_H
#define TEST_HTTP_CLIENT_H
#include <string>
#include <cassert>
#include "WiFiClientSecure.h"
constexpr int HTTP_CODE_OK = 200;
extern int testHttpStatus;
extern std::string testHttpBody;
extern unsigned testHttpCalls;
struct HTTPClient {
  void setConnectTimeout(int) {}
  void setTimeout(int) {}
  bool begin(WiFiClientSecure&, const char* url) {
    assert(std::string(url) == "https://io.adafruit.com/api/v2/test-user/throttle");
    return true;
  }
  void addHeader(const char* key, const char* value) {
    assert(std::string(key) == "X-AIO-Key");
    assert(std::string(value) == "test-key");
  }
  int GET() { ++testHttpCalls; return testHttpStatus; }
  int writeToStream(Stream* stream) {
    return static_cast<int>(stream->write(
        reinterpret_cast<const uint8_t*>(testHttpBody.data()), testHttpBody.size()));
  }
  void end() {}
};
#endif
