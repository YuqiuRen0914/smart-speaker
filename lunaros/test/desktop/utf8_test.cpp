#include "lunaredu_lunaros/desktop/core/utf8.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

int checks = 0;

void check(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

bool supports_test_text(void *, uint32_t codepoint) {
  return codepoint < 0x80 || codepoint == 0x4F60 || codepoint == 0x597D;
}

} // namespace

int main() {
  char output[64] = {};
  size_t replacements = 0;
  const size_t written = lunaredu::desktop::sanitize_utf8(
      "你好🙂A\n", output, sizeof(output), supports_test_text, nullptr,
      &replacements);
  check(std::strcmp(output, "你好?A\n") == 0,
        "unsupported emoji must become a visible ASCII fallback");
  check(replacements == 1, "replacement count must describe missing glyphs");
  check(written == std::strlen(output), "returned size must equal output bytes");

  char truncated[5] = {};
  lunaredu::desktop::sanitize_utf8("你好", truncated, sizeof(truncated),
                                   supports_test_text, nullptr, nullptr);
  check(std::strcmp(truncated, "你") == 0,
        "sanitizing must never split a UTF-8 sequence at capacity");

  const char invalid[] = {'A', static_cast<char>(0xFF), 'B', '\0'};
  lunaredu::desktop::sanitize_utf8(invalid, output, sizeof(output),
                                   supports_test_text, nullptr,
                                   &replacements);
  check(std::strcmp(output, "A?B") == 0 && replacements == 1,
        "invalid UTF-8 must be replaced without stalling");

  std::cout << checks << " UTF-8 sanitization checks passed\n";
  return 0;
}
