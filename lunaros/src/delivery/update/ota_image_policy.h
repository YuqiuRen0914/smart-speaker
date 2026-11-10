#pragma once

#include <stdint.h>
#include <string>

namespace lunaredu::lunaros {

struct OtaCandidateIdentity {
  std::string project_name;
  std::string version;
  uint32_t secure_version = 0;
};

// Accepts only an image for the exact running firmware project and a strictly
// newer semantic version. secure_version may advance but never move backward.
// Only lunaros-bootstrap may opt into cross-project installation using exact
// comma-separated project names; their version sequences are independent.
bool ota_candidate_identity_allowed(const OtaCandidateIdentity &running,
                                    const OtaCandidateIdentity &candidate,
                                    std::string &error,
                                    const std::string &bootstrap_projects = {});

} // namespace lunaredu::lunaros
