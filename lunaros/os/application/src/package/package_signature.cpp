#include "app/package/package_signature.h"
#include "lunaros_signature.h"
#include "lunaros_digest.h"
#include <sdkconfig.h>
#include <array>
#include <cstring>
namespace lunaredu::lunaros::apps {
namespace {
#ifdef CONFIG_LUNAROS_WAMR_AOT
constexpr const char *key_hex = CONFIG_LUNAROS_AOT_PUBLIC_KEY;
constexpr const char *key_id = CONFIG_LUNAROS_AOT_SIGNER_ID;
#else
constexpr const char *key_hex = "", *key_id = "";
#endif
bool configured_signer_valid() {
  if (!*key_id || strlen(key_id)>64 || key_id[0]<'a' || key_id[0]>'z') return false;
  for (const char *p=key_id;*p;++p)
    if (!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='-')) return false;
  return true;
}
}
bool aot_trust_available() {
  if(!configured_signer_valid())return false;
  digest::P256Verifier key(key_hex);
  const bool ready=key.ready();return key.close()&&ready;
}
bool verify_aot_signature(const std::string &digest, const std::string &signer,
                          const std::string &signature_hex, std::string &error) {
  std::array<uint8_t,64> signature{};
  digest::Sha256Value hash{};
  // Domain separation: signature authorizes native code, not arbitrary hashes.
  const std::string message="lunaros.aot.package/v1\n"+digest+"\n";
  if (digest.size()!=64 || signer!=key_id
      || !digest::decode_hex(signature_hex,signature.data(),signature.size())
      || !configured_signer_valid()) { error="AOT 发布者不受信任"; return false; }
  digest::P256Verifier key(key_hex);
  if (!key.ready()) { error="AOT 发布者不受信任"; return false; }
  const bool ok=digest::sha256(reinterpret_cast<const uint8_t *>(message.data()),message.size(),hash) && key.verify(hash,signature.data(),signature.size());
  if(!key.close()){error="AOT 验签密钥清理待重试";return false;}
  if (!ok) error="AOT 发布签名校验失败";
  return ok;
}
}
