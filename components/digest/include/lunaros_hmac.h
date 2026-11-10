#pragma once
#include "lunaros_digest.h"

namespace lunaredu::lunaros::digest {
// Synchronous official PSA HMAC-SHA256. Key derivation and message domains
// belong to callers. A failed key destroy retains one serialized volatile key
// and rejects new imports until retirement succeeds. Failure zeros output.
bool hmac_sha256(const uint8_t *key, size_t key_bytes,
                 const uint8_t *data, size_t data_bytes, Sha256Value &output);
// Retry the retained key without another calculation. No key means success.
bool hmac_sha256_cleanup();
// Erase temporary key material through the official mbedTLS primitive.
void clear_secret(uint8_t *data, size_t size);
}
