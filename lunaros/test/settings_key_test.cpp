#include "persistence/settings/settings_key.h"

#include <cassert>
#include <iostream>

int main() {
  using lunaredu::lunaros::internal::kNvsKeyMaximumLength;
  using lunaredu::lunaros::internal::normalize_nvs_key;

  assert(normalize_nvs_key("short_key") == "short_key");
  assert(normalize_nvs_key("123456789012345") == "123456789012345");
  const auto microphone = normalize_nvs_key("assistant_microphone");
  const auto subtitles = normalize_nvs_key("assistant_subtitles");
  const auto microphone_again = normalize_nvs_key("assistant_microphone");
  assert(microphone.size() == kNvsKeyMaximumLength);
  assert(microphone.front() == 'h');
  assert(microphone == microphone_again);
  assert(microphone != subtitles);
  assert(normalize_nvs_key(nullptr).empty());
  std::cout << "NVS settings key normalization checks passed\n";
  return 0;
}
