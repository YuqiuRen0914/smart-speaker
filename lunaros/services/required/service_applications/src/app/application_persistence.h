#pragma once
#include "app/package/application_package.h"
#include <vector>

namespace lunaredu::lunaros::apps {
// Private codec port: runtime policy and storage I/O stay with the App owner.
bool decode_installed_catalog(const std::string &, std::vector<Application> &);
bool encode_installed_catalog(const std::vector<Application> &, std::string &);
struct ApplicationSaveMetadata {
  uint32_t timestamp = 0, hash = 0;
};
std::string encode_save_metadata(const ApplicationSaveMetadata &);
bool decode_save_metadata(const std::vector<uint8_t> &, ApplicationSaveMetadata &);
} // namespace lunaredu::lunaros::apps
