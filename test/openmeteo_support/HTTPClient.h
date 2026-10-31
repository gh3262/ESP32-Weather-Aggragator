#ifndef TEST_OPENMETEO_HTTP_CLIENT_H
#define TEST_OPENMETEO_HTTP_CLIENT_H

#include <cassert>
#include <string>
#include "WiFiClientSecure.h"

constexpr int HTTP_CODE_OK = 200;
extern int testHttpStatus;
extern std::string testHttpBody;
extern unsigned testHttpCalls;
extern bool testHttpBegin;
extern bool testHttpReadFailure;

struct HTTPClient {
  int connectTimeout = 0;
  int readTimeout = 0;

  void setConnectTimeout(int timeout) { connectTimeout = timeout; }
  void setTimeout(int timeout) { readTimeout = timeout; }

  bool begin(WiFiClientSecure& transport, const char* url) {
    assert(transport.trusted && transport.handshakeTimeout == 5);
    assert(std::string(url) ==
        "https://api.open-meteo.com/v1/forecast?latitude=47.6&longitude=-122.3"
        "&timezone=auto&temperature_unit=fahrenheit&wind_speed_unit=mph"
        "&precipitation_unit=inch&forecast_days=1"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
        "precipitation,rain,showers,snowfall,weather_code,cloud_cover,pressure_msl,"
        "wind_speed_10m,wind_direction_10m,wind_gusts_10m");
    return testHttpBegin;
  }

  int GET() {
    assert(connectTimeout == 4000 && readTimeout == 4000);
    ++testHttpCalls;
    return testHttpStatus;
  }

  int writeToStream(Stream* stream) {
    if (testHttpReadFailure) {
      return -1;
    }
    const size_t split = testHttpBody.size() / 2;
    stream->write(reinterpret_cast<const uint8_t*>(testHttpBody.data()), split);
    stream->write(reinterpret_cast<const uint8_t*>(testHttpBody.data() + split),
                  testHttpBody.size() - split);
    return static_cast<int>(testHttpBody.size());
  }

  void end() {}
};

#endif
