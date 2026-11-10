#include <iostream>
#include <cstdlib>
#include <string>

#include "delivery/update/ota_image_policy.h"

using lunaredu::lunaros::OtaCandidateIdentity;
using lunaredu::lunaros::ota_candidate_identity_allowed;

namespace {

int checks = 0;

void expect(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

bool allowed(const char *current, const char *candidate,
             const char *candidate_project = "desktop-ai-pet-202608-v1",
             uint32_t current_secure = 0, uint32_t candidate_secure = 0,
             std::string *error_output = nullptr) {
  std::string error;
  const bool result = ota_candidate_identity_allowed(
      {"desktop-ai-pet-202608-v1", current, current_secure},
      {candidate_project, candidate, candidate_secure}, error);
  if (error_output != nullptr) *error_output = error;
  return result;
}

} // namespace

int main() {
  expect(allowed("2.0.3", "2.0.4"), "patch upgrade must be accepted");
  expect(allowed("2.0.3", "2.1.0"), "minor upgrade must be accepted");
  expect(allowed("2.9.9", "3.0.0"), "major upgrade must be accepted");
  expect(!allowed("2.0.3", "2.0.3"), "same version must be rejected");
  expect(!allowed("2.0.3", "2.0.2"), "downgrade must be rejected");
  expect(!allowed("2.0.3", "2.0.4", "another-product"),
         "cross-product image must be rejected");
  expect(!allowed("2.0.3", "not-semver"),
         "malformed candidate version must be rejected");
  expect(allowed("2.0.3-beta.1", "2.0.3"),
         "release must supersede its prerelease");
  expect(!allowed("2.0.3", "2.0.4", "desktop-ai-pet-202608-v1", 2, 1),
         "secure version rollback must be rejected");
  expect(allowed("2.0.3", "2.0.4", "desktop-ai-pet-202608-v1", 2, 3),
         "secure version advancement must be accepted");
  std::string error;
  const OtaCandidateIdentity factory{"lunaros-bootstrap", "1.0.0", 2};
  const OtaCandidateIdentity product{"lunaredu-smart-speaker", "0.1.0", 2};
  const std::string targets = "lunaredu-rgb-ota-lamp,lunaredu-smart-speaker";
  expect(ota_candidate_identity_allowed(factory, product, error, targets),
         "bootstrap accepts exact listed project with independent version");
  expect(!ota_candidate_identity_allowed(factory, product, error),
         "bootstrap without explicit targets fails closed");
  expect(!ota_candidate_identity_allowed(factory,
      {"lunaredu-smart", "1.0.0", 2}, error, targets), "prefix is not an approved project");
  expect(!ota_candidate_identity_allowed(factory,
      {"lunaredu-smart-speaker-extra", "1.0.0", 2}, error, targets), "suffix is not an approved project");
  expect(!ota_candidate_identity_allowed(factory,
      {"lunaredu-smart-speaker", "invalid", 2}, error, targets), "bootstrap still validates version");
  expect(!ota_candidate_identity_allowed(factory,
      {"lunaredu-smart-speaker", "1.0.0", 1}, error, targets), "bootstrap still rejects secure rollback");
  expect(!ota_candidate_identity_allowed(factory,
      {"", "1.0.0", 2}, error, targets), "empty project cannot be authorized");
  expect(!ota_candidate_identity_allowed(factory,
      {"lunaros-bootstrap", "1.0.0", 2}, error, "lunaros-bootstrap"),
      "bootstrap self-update still requires newer version");
  expect(!ota_candidate_identity_allowed(product,
      {"lunaredu-rgb-ota-lamp", "2.0.0", 2}, error, targets),
      "business firmware cannot reuse bootstrap exception");
  std::cout << checks << " OTA image policy checks passed\n";
  return 0;
}
