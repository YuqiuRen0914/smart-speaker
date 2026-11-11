#include "interaction/transport/network_xiaozhi_mqtt_udp_channel.h"
#include <cstring>
#include <limits>

namespace {
uint32_t read_be(const uint8_t *p, size_t n) {uint32_t v=0; while(n--) v=(v<<8)|*p++; return v;}
void write_be(uint8_t *p, uint32_t v, size_t n) {for(size_t i=0;i<n;++i)p[n-i-1]=uint8_t(v>>(8*i));}
bool hex16(const std::string &s, std::vector<uint8_t> &out) {
  if(s.size()!=32)return false;
  auto nibble=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
  out.resize(16);
  for(size_t i=0;i<16;++i){int a=nibble(s[i*2]),b=nibble(s[i*2+1]);if(a<0||b<0)return false;out[i]=uint8_t(a*16+b);}
  return true;
}
void iv_for(const uint8_t *header, uint8_t *iv) {
  std::memset(iv,0,12);std::memcpy(iv,header+4,4);iv[7]=header[1];std::memcpy(iv+8,header+12,4);
}
}
XiaozhiMqttUdpChannel::XiaozhiMqttUdpChannel() = default;
XiaozhiMqttUdpChannel::~XiaozhiMqttUdpChannel() {reset();}
void XiaozhiMqttUdpChannel::reset() {
  std::lock_guard<std::mutex> lock(crypto_mutex_);
  if(key_)psa_destroy_key(key_);
  key_=0; configured_=false;server_.clear();port_=0;key_hex_.clear();nonce_hex_.clear();base_nonce_.clear();
  local_sequence_=remote_sequence_=0;last_crypto_error_=0;
}
bool XiaozhiMqttUdpChannel::ready() const {return configured_;}
bool XiaozhiMqttUdpChannel::configure(const XiaozhiMqttUdpConfig &c) {
  std::lock_guard<std::mutex> lock(crypto_mutex_);
  std::vector<uint8_t> key,nonce;
  if(c.server.empty()||c.port<1||c.port>65535||!hex16(c.key_hex,key)||!hex16(c.nonce_hex,nonce)||nonce[0]!=2||nonce[1]!=0)return false;
  auto canonical=[](const std::vector<uint8_t> &bytes) {
    constexpr char hex[]="0123456789abcdef";
    std::string out;for(uint8_t b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
  };
  const auto key_hex=canonical(key), nonce_hex=canonical(nonce);
  // Any repeated key/nonce retains sequence, including alternate hex case or
  // an endpoint update. GCM IV uniqueness belongs to the key, not the host.
  if(configured_ && key_hex_==key_hex &&
      std::memcmp(base_nonce_.data()+4, nonce.data()+4, 4)==0) {
    server_=c.server;port_=c.port;return true;
  }
  psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes,PSA_KEY_TYPE_AES);psa_set_key_bits(&attributes,128);
  psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_ENCRYPT|PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&attributes,PSA_ALG_GCM);
  psa_key_id_t next=0;
  auto err=psa_crypto_init();
  if(err==PSA_SUCCESS)err=psa_import_key(&attributes,key.data(),key.size(),&next);
  psa_reset_key_attributes(&attributes);
  last_crypto_error_=err;if(err!=PSA_SUCCESS)return false;
  if(key_)psa_destroy_key(key_);
  key_=next;base_nonce_=std::move(nonce);server_=c.server;port_=c.port;
  key_hex_=key_hex;nonce_hex_=nonce_hex;local_sequence_=remote_sequence_=0;configured_=true;return true;
}
bool XiaozhiMqttUdpChannel::encrypt_payload(const uint8_t *data,size_t length,uint32_t timestamp,std::string &out) {
  std::lock_guard<std::mutex> lock(crypto_mutex_);out.clear();
  if(!configured_||!data||!length||length>1468||local_sequence_==std::numeric_limits<uint32_t>::max())return false;
  out.resize(16+length+16);auto *h=reinterpret_cast<uint8_t*>(out.data());
  std::memcpy(h,base_nonce_.data(),16);h[0]=2;h[1]=0;write_be(h+2,length,2);write_be(h+8,timestamp,4);write_be(h+12,++local_sequence_,4);
  uint8_t iv[12];iv_for(h,iv);size_t written=0;
  last_crypto_error_=psa_aead_encrypt(key_,PSA_ALG_GCM,iv,12,h,16,data,length,h+16,length+16,&written);
  if(last_crypto_error_!=PSA_SUCCESS||written!=length+16){out.clear();return false;}return true;
}
XiaozhiMqttUdpDecodeResult XiaozhiMqttUdpChannel::decrypt_payload(const uint8_t *data,size_t length) {
  std::lock_guard<std::mutex> lock(crypto_mutex_);XiaozhiMqttUdpDecodeResult r;
  if(!configured_){r.status=XiaozhiMqttUdpDecodeStatus::NotConfigured;return r;}
  if(!data||length<33||length>1500||data[0]!=2||data[1]!=1||
      read_be(data+2,2)+32!=length||std::memcmp(data+4,base_nonce_.data()+4,4)!=0||read_be(data+12,4)==0)return r;
  r.sequence=read_be(data+12,4);r.timestamp_ms=read_be(data+8,4);r.expected_sequence=remote_sequence_+1;
  if(r.sequence<=remote_sequence_){r.status=XiaozhiMqttUdpDecodeStatus::StaleSequence;return r;}
  r.payload.resize(length-32);uint8_t iv[12];iv_for(data,iv);size_t written=0;
  last_crypto_error_=psa_aead_decrypt(key_,PSA_ALG_GCM,iv,12,data,16,data+16,length-16,r.payload.data(),r.payload.size(),&written);
  if(last_crypto_error_!=PSA_SUCCESS||written!=r.payload.size()){r.payload.clear();r.status=XiaozhiMqttUdpDecodeStatus::CryptoError;return r;}
  remote_sequence_=r.sequence;r.status=r.sequence==r.expected_sequence?XiaozhiMqttUdpDecodeStatus::Ok:XiaozhiMqttUdpDecodeStatus::SequenceGap;return r;
}
