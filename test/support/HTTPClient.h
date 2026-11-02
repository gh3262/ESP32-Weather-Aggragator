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
  static unsigned& liveInstances() { static unsigned count = 0; return count; }
  HTTPClient() { ++liveInstances(); }
  ~HTTPClient() { --liveInstances(); }
  void setConnectTimeout(int) {}
  void setTimeout(int) {}
  bool begin(WiFiClientSecure&, const char* url) {
    const std::string address(url);
    assert(address == "https://io.adafruit.com/api/v2/test-user/throttle" ||
           (address.find("https://io.adafruit.com/api/v2/test-user/feeds/omp-") == 0 &&
            address.substr(address.size() - 5) == "/data"));
    return true;
  }
  void addHeader(const char* key, const char* value) {
    assert((std::string(key) == "X-AIO-Key" && std::string(value) == "test-key") ||
           (std::string(key) == "Content-Type" && std::string(value) == "application/json"));
  }
  int GET() { ++testHttpCalls; return testHttpStatus; }
  int POST(const char* body) {
    assert(body && std::string(body).find("\"created_at\":") != std::string::npos);
    assert(std::string(body).find("\"lat\":") == std::string::npos);
    ++testHttpCalls;
    return testHttpStatus;
  }
  int writeToStream(Stream* stream) {
    return static_cast<int>(stream->write(
        reinterpret_cast<const uint8_t*>(testHttpBody.data()), testHttpBody.size()));
  }
  void end() {}
};
#endif
