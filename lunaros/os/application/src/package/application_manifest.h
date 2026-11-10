#pragma once
#include "app/package/application_package.h"
#include <optional>
#include <vector>

namespace lunaredu::lunaros::apps {
// Decoded package facts. Admission policy never depends on a wire codec.
struct ApplicationManifestFacts {
  Application application;
  std::string schema, icon_kind;
  uint32_t abi = 0;
  int canvas_width = 0, canvas_height = 0;
  std::optional<uint32_t> memory_pages;
  std::vector<std::string> permissions;
};
bool validate_application_manifest(const ApplicationManifestFacts &, Application &, std::string &error);
} // namespace lunaredu::lunaros::apps
