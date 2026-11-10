#pragma once
#include "lunaros_digest.h"
namespace lunaredu::lunaros::digest {
// Retry detached key retirement without touching live verifier borrows.
bool verification_cleanup();
// Generic cryptographic verifier. Trust selection and signature domain belong
// to the caller. Eight boot-lived slots bound simultaneous PSA keys; failed
// destruction transfers ownership back to that pool for subsequent retry.
class P256Verifier {
 public:
  explicit P256Verifier(const char *uncompressed_public_key_hex);
  ~P256Verifier();
  P256Verifier(const P256Verifier &)=delete;
  P256Verifier &operator=(const P256Verifier &)=delete;
  bool ready() const { return ready_; }
  // Revokes verification even if destruction fails; may be retried.
  bool close();
  bool verify(const Sha256Value &,const uint8_t *signature,size_t size) const;
 private:
  void *state_=nullptr;
  bool ready_=false;
};

// PKCS#1 DER RSA public key. RS256 uses PKCS#1 v1.5 with SHA-256, never
// algorithm negotiation from untrusted input. No trust/root selection here.
class RsaSha256Verifier {
 public:
  RsaSha256Verifier(const uint8_t *der, size_t size, size_t bits);
  ~RsaSha256Verifier();
  RsaSha256Verifier(const RsaSha256Verifier &)=delete;
  RsaSha256Verifier &operator=(const RsaSha256Verifier &)=delete;
  bool ready() const { return ready_; }
  bool close();
  bool verify(const Sha256Value &, const uint8_t *signature, size_t size) const;
 private:
  void *state_=nullptr;
  size_t signature_size_=0;
  bool ready_=false;
};
}
