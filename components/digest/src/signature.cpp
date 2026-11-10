#include "lunaros_signature.h"
#include <mutex>
namespace lunaredu::lunaros::digest {
namespace {
enum class Ownership { Free, Borrowed, Retiring };
struct KeyState { mbedtls_svc_key_id_t key{};Ownership ownership=Ownership::Free; };
struct VerificationOwner {std::mutex mutex;std::array<KeyState,8> keys{};} owner;
bool cleanup_locked() {
  bool ok=true;
  for(auto &slot:owner.keys)if(slot.ownership==Ownership::Retiring) {
    if(psa_destroy_key(slot.key)==PSA_SUCCESS)slot=KeyState{};else ok=false;
  }
  return ok;
}
// Reserve a boot-lived slot before importing. Destruction never needs to
// allocate memory in order to keep a failed SDK resource available for retry.
void *import_verification_key(const psa_key_attributes_t &attributes,const uint8_t *data,size_t size) {
  std::lock_guard lock(owner.mutex);
  if(!cleanup_locked())return nullptr;
  for(auto &slot:owner.keys)if(slot.ownership==Ownership::Free) {
    if(psa_import_key(&attributes,data,size,&slot.key)!=PSA_SUCCESS)return nullptr;
    slot.ownership=Ownership::Borrowed;return &slot;
  }
  return nullptr;
}
mbedtls_svc_key_id_t verification_key(void *state) {return static_cast<KeyState *>(state)->key;}
bool release_verification_key(void *state) {
  if(!state)return true;
  std::lock_guard lock(owner.mutex);auto &slot=*static_cast<KeyState *>(state);
  if(psa_destroy_key(slot.key)!=PSA_SUCCESS)return false;
  slot=KeyState{};return true;
}
void abandon_verification_key(void *state) {
  std::lock_guard lock(owner.mutex);static_cast<KeyState *>(state)->ownership=Ownership::Retiring;
}
}
bool verification_cleanup(){std::lock_guard lock(owner.mutex);return cleanup_locked();}
P256Verifier::P256Verifier(const char *hex) {
  std::array<uint8_t,65> bytes{};
  if(!hex || !decode_hex(hex,bytes.data(),bytes.size()) || bytes[0]!=4 || !initialize())return;
  psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&attributes,256);
  psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_VERIFY_HASH);
  psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_256));
  state_=import_verification_key(attributes,bytes.data(),bytes.size());ready_=state_!=nullptr;
  psa_reset_key_attributes(&attributes);
}
P256Verifier::~P256Verifier(){if(!close())abandon_verification_key(state_);}
bool P256Verifier::close(){ready_=false;if(!release_verification_key(state_))return false;state_=nullptr;return true;}
bool P256Verifier::verify(const Sha256Value &hash,const uint8_t *signature,size_t size)const {
  return ready_ && signature && size==64 &&
    psa_verify_hash(verification_key(state_),PSA_ALG_ECDSA(PSA_ALG_SHA_256),hash.data(),hash.size(),signature,size)==PSA_SUCCESS;
}
RsaSha256Verifier::RsaSha256Verifier(const uint8_t *der,size_t size,size_t bits) {
  if (!der || size==0 || size>1024 ||
      (bits!=2048 && bits!=3072 && bits!=4096) || !initialize()) return;
  psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes,PSA_KEY_TYPE_RSA_PUBLIC_KEY);
  psa_set_key_bits(&attributes,bits);
  psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_VERIFY_HASH);
  psa_set_key_algorithm(&attributes,PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256));
  state_=import_verification_key(attributes,der,size);ready_=state_!=nullptr;
  psa_reset_key_attributes(&attributes);
  if (ready_) signature_size_=bits/8;
}
RsaSha256Verifier::~RsaSha256Verifier(){if(!close())abandon_verification_key(state_);}
bool RsaSha256Verifier::close(){ready_=false;if(!release_verification_key(state_))return false;state_=nullptr;return true;}
bool RsaSha256Verifier::verify(const Sha256Value &hash,const uint8_t *signature,size_t size)const {
  return ready_ && signature && size==signature_size_ &&
      psa_verify_hash(verification_key(state_),PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256),
                      hash.data(),hash.size(),signature,size)==PSA_SUCCESS;
}
}
