#pragma once
#include <ArduinoJson.h>
#include <array>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros::platform_link {

// PlatformLink owns automatic assignment retry policy, not image installation.
class AutomaticOtaRetryGuard {
 public:
  bool allowed(const std::string &key, int64_t epoch, uint32_t now);
  bool reserve(const std::string &key, int64_t epoch, uint32_t now);
  bool reset(); // Explicit operator action; storage failure stays closed.
  // context_key uses empty assignment; success releases this digest only in
  // the current environment/account, including its prior assignments.
  bool confirmed(const std::string &context_key, uint32_t now);
  void fill_status(JsonObject target) const;
  const std::string &error() const { return error_; }
 private:
  struct Entry { std::string key; unsigned attempts = 0; int64_t window = 0, last = 0; uint32_t observed = 0, attempted = 0; bool local_attempt = false; };
  bool load(uint32_t now);
  bool save();
  std::array<Entry,4> entries_{};
  bool loaded_ = false;
  std::string error_;
  unsigned selected_attempts_ = 0;
};

std::string automatic_ota_key(const std::string &origin, const std::string &account,
    const std::string &assignment, const std::string &sha256);
void fill_automatic_ota_status(JsonObject);
bool reset_automatic_ota_retry(std::string &error);

} // namespace lunaredu::lunaros::platform_link
