#include "lunaros_hmac.h"
#include <mutex>
#include <mbedtls/platform_util.h>

namespace lunaredu::lunaros::digest {
namespace {
struct HmacOwner {
  std::mutex mutex;
  mbedtls_svc_key_id_t key{};
  bool live = false;
} hmac_owner;
// Caller holds mutex, including retries after a previous failed destroy.
bool retire_hmac_key() {
  if (!hmac_owner.live) return true;
  if (psa_destroy_key(hmac_owner.key) != PSA_SUCCESS) return false;
  hmac_owner.key = {}; hmac_owner.live = false;
  return true;
}
}
bool hmac_sha256_cleanup() {
  std::lock_guard lock(hmac_owner.mutex);
  return retire_hmac_key();
}
void clear_secret(uint8_t *data, size_t size) {
  mbedtls_platform_zeroize(data,size);
}
bool hmac_sha256(const uint8_t *key, size_t key_bytes,
                 const uint8_t *data, size_t data_bytes, Sha256Value &output) {
  output.fill(0);
  if ((!key && key_bytes) || (!data && data_bytes)) return false;
  // A volatile PSA key can fail to retire. Keep exactly one boot-lived slot,
  // and retry it before accepting another operation; never lose its handle.
  std::lock_guard lock(hmac_owner.mutex);
  if (!retire_hmac_key() || !initialize()) return false;
  struct Scratch {
    Sha256Value key{}, result{};
    ~Scratch() { clear_secret(key.data(), key.size()); clear_secret(result.data(), result.size()); }
  } scratch;
  // PSA rejects zero-length keys. A single zero byte has the same RFC 2104
  // block padding as an empty key. Reduce long keys with the official hash.
  const uint8_t zero = 0;
  if (!key_bytes) { key = &zero; key_bytes = 1; }
  else if (key_bytes > 64) {
    if (!sha256(key, key_bytes, scratch.key)) return false;
    key = scratch.key.data(); key_bytes = scratch.key.size();
  }
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
  psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
  const auto imported = psa_import_key(&attributes, key, key_bytes, &hmac_owner.key);
  psa_reset_key_attributes(&attributes);
  if (imported != PSA_SUCCESS) return false;
  hmac_owner.live = true;
  size_t written = 0;
  const auto status = psa_mac_compute(hmac_owner.key, PSA_ALG_HMAC(PSA_ALG_SHA_256),
      data_bytes ? data : &zero, data_bytes, scratch.result.data(), scratch.result.size(), &written);
  const bool retired = retire_hmac_key();
  if (status != PSA_SUCCESS || written != output.size() || !retired) return false;
  output = scratch.result;
  return true;
}
}
