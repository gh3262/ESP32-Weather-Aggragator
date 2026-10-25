#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H

#include <stdint.h>
#include <string>
#include <cstdio>

class Stream {
 public:
  virtual ~Stream() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
};

struct TestSerial {
  std::string output;

  void println(const char* line) {
    output += line;
    output += '\n';
  }

  void printf(const char* message) { output += message; }

  template <typename... Args>
  void printf(const char* format, Args... args) {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    output += buffer;
  }
};

extern TestSerial Serial;
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
struct TestEsp {
  uint32_t getFreeHeap() const { return 200000; }
  uint32_t getMinFreeHeap() const { return 180000; }
};
extern TestEsp ESP;

#endif
