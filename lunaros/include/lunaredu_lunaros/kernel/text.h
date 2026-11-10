#pragma once

#include <cctype>
#include <string>

namespace lunaredu::lunaros::text {

inline std::string trim_copy(const std::string &value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }

  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }

  return value.substr(start, end - start);
}

inline std::string to_std_string(const char *value) {
  return value ? std::string(value) : std::string();
}

} // namespace lunaredu::lunaros::text
