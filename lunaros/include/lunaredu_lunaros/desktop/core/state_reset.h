#pragma once
#include <array>
#include <string>
namespace lunaredu::desktop::internal {
inline void reset_value(std::string &value) { value.clear(); }
template<typename T> void reset_value(T &value) {
  const T empty{};
  value = empty; // copy assignment retains owned strings inside small entries
}
template<typename T, size_t N> void reset_value(std::array<T, N> &values) {
  for (auto &value : values) reset_value(value);
}
} // namespace lunaredu::desktop::internal
