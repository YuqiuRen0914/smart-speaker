#include <iostream>
#include <string>

#include "delivery/update/ota_version.h"

namespace {

int failures = 0;

void expect(const std::string &input, const std::string &expected) {
  const std::string actual =
      lunaredu::lunaros::internal::normalize_ota_release_version(input);
  if (actual != expected) {
    std::cerr << "version " << input << " normalized to " << actual
              << ", expected " << expected << std::endl;
    ++failures;
  }
}

} // namespace

int main() {
  expect("020105", "2.1.5");
  expect("20105", "2.1.5");
  expect("020109", "2.1.9");
  expect("020110", "2.1.10");
  expect("020111", "2.1.11");
  expect("2.1.5", "2.1.5");
  expect("v2.1.5", "v2.1.5");
  expect("2026", "2026");
  expect(" 020105 ", "2.1.5");
  if (failures != 0) {
    return 1;
  }
  std::cout << "OTA release version normalization checks passed" << std::endl;
  return 0;
}
