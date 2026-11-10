#include "lunaros_digest.h"
#include <algorithm>
#include <limits>
#include <mutex>
extern "C" {
#include <mbedtls/constant_time.h>
}

namespace lunaredu::lunaros::digest {
namespace {
enum class Ownership { Free, Borrowed, Retiring };
struct HashState {
  psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
  Ownership ownership = Ownership::Free;
};
struct CryptoOwner {
  std::mutex mutex;
  // Bounded admission before any SDK allocation. No allocation is required
  // when transferring a failed destructor's resource back to the owner.
  std::array<HashState,8> hashes{};
} owner;
bool cleanup_locked() {
  bool ok=true;
  for(auto &slot:owner.hashes)if(slot.ownership==Ownership::Retiring) {
    if(psa_hash_abort(&slot.operation)==PSA_SUCCESS)slot=HashState{};
    else ok=false;
  }
  return ok;
}
}
bool cleanup() { std::lock_guard lock(owner.mutex);return cleanup_locked(); }
bool constant_time_equal(const void *a, const void *b, size_t size) {
  if (!size) return true;
  return a && b && mbedtls_ct_memcmp(a, b, size) == 0;
}
Sha256::~Sha256() {
  if(!abort()) {
    std::lock_guard lock(owner.mutex);
    static_cast<HashState *>(state_)->ownership=Ownership::Retiring;
    state_=nullptr;
  }
}
bool Sha256::abort() {
  if (!state_) return true;
  std::lock_guard lock(owner.mutex);
  auto &slot=*static_cast<HashState *>(state_);
  const auto status = psa_hash_abort(&slot.operation);
  if (status != PSA_SUCCESS) {
    last_status_ = status;
    failed_ = true;
    return false;
  }
  slot=HashState{};state_=nullptr;
  failed_ = false;
  return true;
}
bool initialize() { return cleanup() && psa_crypto_init() == PSA_SUCCESS; }
bool Sha256::begin() {
  if (!abort()) return false;
  {
    std::lock_guard lock(owner.mutex);
    if(!cleanup_locked()){last_status_=PSA_ERROR_BAD_STATE;return false;}
    last_status_ = psa_crypto_init();
    if(last_status_!=PSA_SUCCESS)return false;
    for(auto &slot:owner.hashes)if(slot.ownership==Ownership::Free) {
      slot.ownership=Ownership::Borrowed;state_=&slot;break;
    }
  }
  if(!state_){last_status_=PSA_ERROR_INSUFFICIENT_MEMORY;return false;}
  failed_ = false;
  last_status_ = psa_hash_setup(&static_cast<HashState *>(state_)->operation, PSA_ALG_SHA_256);
  if (last_status_ == PSA_SUCCESS) return true;
  failed_ = true;
  (void)abort();
  return false;
}
bool Sha256::update(const uint8_t *data, size_t size) {
  if (!state_ || failed_) return false;
  last_status_ = (!data && size) ? PSA_ERROR_INVALID_ARGUMENT : psa_hash_update(&static_cast<HashState *>(state_)->operation, data, size);
  if (last_status_ != PSA_SUCCESS) {
    failed_ = true;
    (void)abort();
    return false;
  }
  return true;
}
bool Sha256::finish(Sha256Value &output) {
  output.fill(0);
  if (!state_ || failed_) return false;
  size_t length = 0;
  last_status_ = psa_hash_finish(&static_cast<HashState *>(state_)->operation, output.data(), output.size(), &length);
  if (last_status_ == PSA_SUCCESS && length != output.size()) last_status_ = PSA_ERROR_BAD_STATE;
  if (last_status_ != PSA_SUCCESS) {
    failed_ = true;
    output.fill(0);
    (void)abort();
    return false;
  }
  {std::lock_guard lock(owner.mutex);*static_cast<HashState *>(state_)=HashState{};state_=nullptr;}
  return true;
}
bool sha256(const uint8_t *data, size_t size, Sha256Value &output) {
  output.fill(0);
  Sha256 stream;
  return stream.begin() && stream.update(data, size) && stream.finish(output);
}
std::string hex(const uint8_t *data, size_t size) {
  if ((!data && size) || size > std::numeric_limits<size_t>::max()/2) return {};
  constexpr char digits[] = "0123456789abcdef";
  std::string result(size*2, '0');
  for (size_t i=0; i<size; ++i) {
    result[i*2]=digits[data[i]>>4]; result[i*2+1]=digits[data[i]&15];
  }
  return result;
}
std::string sha256_hex(const uint8_t *data, size_t size) {
  Sha256Value value{};
  return sha256(data,size,value) ? hex(value.data(),value.size()) : std::string{};
}
bool decode_hex(std::string_view text, uint8_t *output, size_t size, HexCase casing) {
  if ((!output && size) || size > std::numeric_limits<size_t>::max()/2 || text.size()!=size*2)
    return false;
  const auto digit=[casing](char c) {
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(casing==HexCase::Any && c>='A' && c<='F')return c-'A'+10;
    return -1;
  };
  // Preflight prevents partially decoded values from escaping on failure.
  if(!std::all_of(text.begin(),text.end(),[&](char c){return digit(c)>=0;}))return false;
  for(size_t i=0;i<size;++i)output[i]=uint8_t(digit(text[i*2])*16+digit(text[i*2+1]));
  return true;
}
} // namespace lunaredu::lunaros::digest
