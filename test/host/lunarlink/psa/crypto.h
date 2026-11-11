#pragma once
// Host-only PSA adapter backed by OpenSSL. Production includes ESP-IDF PSA.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <array>
#include <openssl/evp.h>
using psa_key_id_t=uint32_t;
using psa_status_t=int;
struct psa_key_attributes_t {int type=0,bits=0,usage=0,algorithm=0;};
#define PSA_KEY_ATTRIBUTES_INIT {}
constexpr int PSA_SUCCESS=0,PSA_KEY_TYPE_AES=1,PSA_KEY_USAGE_ENCRYPT=1,PSA_KEY_USAGE_DECRYPT=2,PSA_ALG_GCM=3;
inline auto &host_keys(){static std::map<psa_key_id_t,std::array<uint8_t,16>> keys;return keys;}
inline void psa_set_key_type(psa_key_attributes_t *a,int v){a->type=v;}
inline void psa_set_key_bits(psa_key_attributes_t *a,int v){a->bits=v;}
inline void psa_set_key_usage_flags(psa_key_attributes_t *a,int v){a->usage=v;}
inline void psa_set_key_algorithm(psa_key_attributes_t *a,int v){a->algorithm=v;}
inline void psa_reset_key_attributes(psa_key_attributes_t*){}
inline int psa_crypto_init(){return 0;}
inline int psa_import_key(const psa_key_attributes_t *a,const uint8_t *p,size_t n,psa_key_id_t *id){
 assert(a->type==PSA_KEY_TYPE_AES&&a->bits==128&&a->usage==3&&a->algorithm==PSA_ALG_GCM&&n==16);
 static uint32_t next=0;*id=++next;std::memcpy(host_keys()[*id].data(),p,16);return 0;
}
inline int psa_destroy_key(psa_key_id_t id){host_keys().erase(id);return 0;}
inline int psa_aead_encrypt(psa_key_id_t id,int alg,const uint8_t *iv,size_t ivn,const uint8_t *aad,size_t aadn,
 const uint8_t *in,size_t n,uint8_t *out,size_t cap,size_t *written){
 assert(alg==PSA_ALG_GCM&&ivn==12&&cap>=n+16);auto *c=EVP_CIPHER_CTX_new();assert(c);int len=0,total=0;
 int ok=EVP_EncryptInit_ex(c,EVP_aes_128_gcm(),nullptr,host_keys().at(id).data(),iv);
 ok&=EVP_EncryptUpdate(c,nullptr,&len,aad,int(aadn));ok&=EVP_EncryptUpdate(c,out,&len,in,int(n));total=len;
 ok&=EVP_EncryptFinal_ex(c,out+total,&len);total+=len;ok&=EVP_CIPHER_CTX_ctrl(c,EVP_CTRL_GCM_GET_TAG,16,out+total);
 EVP_CIPHER_CTX_free(c);*written=total+16;return ok?0:-1;
}
inline int psa_aead_decrypt(psa_key_id_t id,int alg,const uint8_t *iv,size_t ivn,const uint8_t *aad,size_t aadn,
 const uint8_t *in,size_t n,uint8_t *out,size_t cap,size_t *written){
 assert(alg==PSA_ALG_GCM&&ivn==12&&n>=16&&cap>=n-16);auto *c=EVP_CIPHER_CTX_new();assert(c);int len=0,total=0;
 int ok=EVP_DecryptInit_ex(c,EVP_aes_128_gcm(),nullptr,host_keys().at(id).data(),iv);
 ok&=EVP_DecryptUpdate(c,nullptr,&len,aad,int(aadn));ok&=EVP_DecryptUpdate(c,out,&len,in,int(n-16));total=len;
 ok&=EVP_CIPHER_CTX_ctrl(c,EVP_CTRL_GCM_SET_TAG,16,const_cast<uint8_t*>(in+n-16));
 ok&=EVP_DecryptFinal_ex(c,out+total,&len);total+=len;EVP_CIPHER_CTX_free(c);*written=total;return ok?0:-1;
}
