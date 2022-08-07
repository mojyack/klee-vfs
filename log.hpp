#pragma once
#include <array>
#include <cstdio>
#include <iostream>
#include <span>
#include <string_view>

enum class LogLevel {
    Error = 3,
    Warn  = 4,
    Info  = 6,
    Debug = 7,
};

class Logger {
  public:
    auto operator()(const LogLevel level, const char* const format, ...) -> int {
        static auto buffer = std::array<char, 1024>();

        va_list ap;
        va_start(ap, format);
        const auto result = vsnprintf(buffer.data(), buffer.size(), format, ap);
        va_end(ap);
        std::cout << std::string_view(buffer.data(), result);
        return result;
    }
};

inline auto logger = Logger();
