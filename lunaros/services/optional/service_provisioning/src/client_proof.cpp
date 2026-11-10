#include "lunaredu_device_profiles.h"
#include "client_proof.h"
#include "system/identity.h"
#include "lunaros_hmac.h"
namespace lunaredu::lunaros {
std::string device_confirmation(uint32_t nonce, const std::string &proof) {
  if (proof.empty()) return {};
  const auto message = std::string(protocol::profiles::provisioning_proof_prefix) + hardware_serial_number() +
                       ":" + std::to_string(nonce);
  digest::Sha256Value mac{};
  if (!digest::hmac_sha256(reinterpret_cast<const uint8_t *>(proof.data()), proof.size(),
      reinterpret_cast<const uint8_t *>(message.data()), message.size(), mac)) return {};
  auto tag = digest::hex(mac.data(), mac.size());
  digest::clear_secret(mac.data(), mac.size());
  return tag;
}
} // namespace lunaredu::lunaros
