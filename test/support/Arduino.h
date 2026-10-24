#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H

#include <stdint.h>
#include <string>
#include <cstdio>

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

#endif
