#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
// Owned by one confirmed LP client, accessed on the existing Network executor.
// This adapter starts no radio, transport, task, timer or persistence owner.
class OfficialProvisioning {
 public:
  OfficialProvisioning() = default;
  ~OfficialProvisioning();
  OfficialProvisioning(const OfficialProvisioning &) = delete;
  OfficialProvisioning &operator=(const OfficialProvisioning &) = delete;
  bool exchange(KernelRuntime &, uint32_t nonce, const std::string &proof,
                const char *endpoint, const std::string &input,
                std::string &response, std::string &error);
 private:
  struct Impl;
  Impl *impl_ = nullptr;
  void reset();
};
}
