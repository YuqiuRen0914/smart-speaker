#pragma once
// Host-only ESP AES adapter using OpenSSL; actual channel code is under test.
#include <cstddef>
#include <cstring>
#include <openssl/evp.h>
struct esp_aes_context { unsigned char key[16]{}; };
inline void esp_aes_init(esp_aes_context *) {}
inline void esp_aes_free(esp_aes_context *) {}
inline int esp_aes_setkey(esp_aes_context *ctx, const unsigned char *key, unsigned bits) {
  if (bits != 128) return -1;
  std::memcpy(ctx->key, key, 16); return 0;
}
inline int esp_aes_crypt_ctr(esp_aes_context *ctx, size_t length, size_t *offset,
                           unsigned char iv[16], unsigned char block[16],
                           const unsigned char *input, unsigned char *output) {
  auto *cipher = EVP_CIPHER_CTX_new();
  if (!cipher) return -1;
  if (EVP_EncryptInit_ex(cipher, EVP_aes_128_ecb(), nullptr, ctx->key, nullptr) != 1) {
    EVP_CIPHER_CTX_free(cipher); return -1;
  }
  EVP_CIPHER_CTX_set_padding(cipher, 0);
  for (size_t i = 0; i < length; ++i) {
    if (*offset == 0) {
      int count = 0;
      if (EVP_EncryptUpdate(cipher, block, &count, iv, 16) != 1 || count != 16) {
        EVP_CIPHER_CTX_free(cipher); return -1;
      }
      for (int j = 15; j >= 0 && ++iv[j] == 0; --j) {}
    }
    output[i] = input[i] ^ block[*offset];
    *offset = (*offset + 1) & 15;
  }
  EVP_CIPHER_CTX_free(cipher); return 0;
}
