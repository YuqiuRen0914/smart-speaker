#pragma once

#include <stdint.h>
#include <string>

namespace lunaredu::lunaros {

struct OtaCandidateIdentity {
  std::string project_name;
  std::string version;
  uint32_t secure_version = 0;
};
// Explicit Boot maintenance has no installer/administrator version exemption.
// same_image is supplied only after descriptor identity has been compared.
bool ota_boot_candidate_identity_allowed(const OtaCandidateIdentity &previous,
    const OtaCandidateIdentity &candidate, std::string &error, bool same_image = false);

// Ordinary products require the same project, a newer semantic version and
// a secure_version that does not move backward.
// Bootstrap installs an application under its explicit installer contract.
// The legacy maintenance flag grants no product/version exemption.
// ESP-IDF image, chip, signature and eFuse checks remain in the OTA writer.
bool ota_candidate_identity_allowed(const OtaCandidateIdentity &running,
                                    const OtaCandidateIdentity &candidate,
                                    std::string &error,
                                    bool maintenance_authorized = false);

} // namespace lunaredu::lunaros
