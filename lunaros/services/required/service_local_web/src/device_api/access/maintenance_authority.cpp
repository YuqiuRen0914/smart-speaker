#include "device_api/access/maintenance_authority.h"
#include "lunaros_signature.h"
#include <algorithm>
#include <cstring>
#include <string_view>
#include <mbedtls/base64.h>
#include <mbedtls/asn1write.h>
#include <utility>

namespace lunaredu::lunaros::local_web {
namespace {
constexpr uint32_t kChallengeMs=120000,kKeysMs=3600000;
bool fail(std::string &error,const char *code){error=code;return false;}
bool hex(const std::string &s,size_t count) {
  return s.size()==count && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
std::string encode_hex(const std::array<uint8_t,32> &bytes) {
  return digest::hex(bytes.data(),bytes.size());
}
bool equal(const std::string &a,const char *b) {
  if(!b || a.size()!=std::strlen(b))return false;
  return digest::constant_time_equal(a.data(),b,a.size());
}
bool base64url(std::string_view source,std::vector<uint8_t> &out) {
  out.clear();if(source.empty()||source.size()>8192||source.size()%4==1)return false;
  std::string standard(source);
  // URL alphabet and omitted padding are protocol policy, not a second codec.
  for(char &c:standard) {
    if(c=='-')c='+';else if(c=='_')c='/';
    else if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')))return false;
  }
  while(standard.size()%4)standard+='=';
  std::vector<uint8_t> decoded(standard.size()/4*3);
  size_t written=0;
  if(mbedtls_base64_decode(decoded.data(),decoded.size(),&written,
      reinterpret_cast<const unsigned char *>(standard.data()),standard.size())!=0)return false;
  std::vector<unsigned char> canonical(standard.size()+1);
  size_t encoded=0;
  if(mbedtls_base64_encode(canonical.data(),canonical.size(),&encoded,decoded.data(),written)!=0 ||
      encoded!=standard.size() || std::memcmp(canonical.data(),standard.data(),encoded))return false;
  decoded.resize(written);out=std::move(decoded);return true;
}
bool rsa_public_der(const std::vector<uint8_t> &modulus,const std::vector<uint8_t> &exponent,
                    std::vector<uint8_t> &out) {
  // Public, bounded DER writer. Trust, supported key sizes and JWK integer
  // canonicality are checked by the caller before entering the codec.
  std::array<unsigned char,544> buffer{};
  auto *cursor=buffer.data()+buffer.size();
  if(mbedtls_asn1_write_integer(&cursor,buffer.data(),exponent.data(),exponent.size())<0 ||
     mbedtls_asn1_write_integer(&cursor,buffer.data(),modulus.data(),modulus.size())<0)return false;
  const size_t body=buffer.data()+buffer.size()-cursor;
  if(mbedtls_asn1_write_len(&cursor,buffer.data(),body)<0 ||
     mbedtls_asn1_write_tag(&cursor,buffer.data(),MBEDTLS_ASN1_CONSTRUCTED|MBEDTLS_ASN1_SEQUENCE)<0)return false;
  out.assign(cursor,buffer.data()+buffer.size());return true;
}
bool uuid_valid(const std::string &s) {
  if(s.size()!=36)return false;
  for(size_t i=0;i<s.size();++i) {if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;}return true;
}
bool serial_valid(const std::string &s) {
  return !s.empty()&&s.size()<=64&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';});
}
bool exact_fields(JsonObjectConst object,const std::initializer_list<const char *> &allowed) {
  for(auto field:object){bool found=false;for(auto key:allowed)if(field.key()==key){found=true;break;}if(!found)return false;}return true;
}
bool strings_safe(JsonVariantConst value) {
  if(value.is<const char *>()) {
    const auto text=value.as<JsonString>();return text.c_str()&&std::strlen(text.c_str())==text.size();
  }
  if(value.is<JsonObjectConst>())for(auto field:value.as<JsonObjectConst>()) {
    if(std::strlen(field.key().c_str())!=field.key().size()||!strings_safe(field.value()))return false;
  }
  if(value.is<JsonArrayConst>())for(auto item:value.as<JsonArrayConst>())if(!strings_safe(item))return false;
  return true;
}
bool private_callback(const std::string &origin) {
  if(origin.rfind("http://",0)!=0||origin.size()>80)return false;
  const auto authority=origin.substr(7);const auto colon=authority.find(':');
  const auto ip=authority.substr(0,colon);unsigned octets[4]{};size_t index=0;
  for(unsigned n=0;n<4;++n){size_t start=index;while(index<ip.size()&&ip[index]>='0'&&ip[index]<='9'){octets[n]=octets[n]*10+static_cast<unsigned>(ip[index++]-'0');if(octets[n]>255)return false;}
    if(index==start||(index-start>1&&ip[start]=='0'))return false;
    if(n<3){if(index==ip.size()||ip[index++]!='.')return false;}else if(index!=ip.size())return false;}
  if(!(octets[0]==10||(octets[0]==172&&octets[1]>=16&&octets[1]<=31)||(octets[0]==192&&octets[1]==168)))return false;
  if(colon!=std::string::npos){const auto port=authority.substr(colon+1);unsigned v=0;if(port.empty()||port.size()>5)return false;for(char c:port){if(c<'0'||c>'9')return false;v=v*10+static_cast<unsigned>(c-'0');}if(!v||v>65535)return false;}
  return true;
}
}

bool MaintenanceAuthority::install_trusted_jwks(const std::string &text,uint32_t now,std::string &error) {
  if(text.empty()||text.size()>16384)return fail(error,"maintenance_jwks_invalid");
  JsonDocument document;
  if(deserializeJson(document,text)||!strings_safe(document.as<JsonVariantConst>())||!document["keys"].is<JsonArrayConst>()||document["keys"].size()>16)return fail(error,"maintenance_jwks_invalid");
  std::vector<Key> next;
  for(JsonVariantConst raw:document["keys"].as<JsonArrayConst>()) {
    if(!raw.is<JsonObjectConst>())return fail(error,"maintenance_jwks_invalid");
    if(std::string(raw["kty"]|"")!="RSA")continue;
    if(std::string(raw["alg"]|"")!="RS256"||std::string(raw["use"]|"")!="sig"||!raw["d"].isNull())continue;
    Key key;key.kid=raw["kid"]|"";
    if(key.kid.empty()||key.kid.size()>128||std::any_of(key.kid.begin(),key.kid.end(),[](unsigned char c){return c<33||c>126;}))return fail(error,"maintenance_jwks_invalid");
    if(std::any_of(next.begin(),next.end(),[&key](const Key &k){return k.kid==key.kid;}))return fail(error,"maintenance_jwks_ambiguous");
    std::vector<uint8_t> n,e;
    const std::string modulus=raw["n"]|"",exponent=raw["e"]|"";
    if(modulus.size()>684||exponent.size()>11||!base64url(modulus,n)||!base64url(exponent,e)||n.empty()||e.empty()||!(n[0]&128)||e[0]==0||!(e.back()&1))return fail(error,"maintenance_jwks_invalid");
    key.bits=n.size()*8;if(key.bits!=2048&&key.bits!=3072&&key.bits!=4096)return fail(error,"maintenance_jwks_invalid");
    if(e.size()==1&&e[0]<3)return fail(error,"maintenance_jwks_invalid");
    if(!rsa_public_der(n,e,key.der))return fail(error,"maintenance_jwks_invalid");
    digest::RsaSha256Verifier verifier(key.der.data(),key.der.size(),key.bits);
    const bool usable=verifier.ready();
    if(!verifier.close())return fail(error,"maintenance_key_retirement_pending");
    if(!usable)return fail(error,"maintenance_jwks_key_unusable");
    next.push_back(std::move(key));
  }
  if(next.empty())return fail(error,"maintenance_jwks_no_rs256");
  std::lock_guard<std::mutex> lock(mutex_);keys_=std::move(next);keys_at_=now;error.clear();return true;
}
bool MaintenanceAuthority::keys_ready(uint32_t now) const {
  std::lock_guard<std::mutex> lock(mutex_);return !keys_.empty()&&static_cast<uint32_t>(now-keys_at_)<kKeysMs;
}
bool MaintenanceAuthority::issue(const std::string &serial,const std::string &uuid,const std::string &state,const std::string &callback,const std::string &opener,const std::array<uint8_t,32> &entropy,uint32_t now,int64_t epoch,JsonObject out,std::string &error) {
  if(!serial_valid(serial)||!uuid_valid(uuid)||!hex(state,64)||!private_callback(callback)||opener.size()>256||epoch<1700000000||std::all_of(entropy.begin(),entropy.end(),[](uint8_t b){return b==0;}))return fail(error,"maintenance_request_invalid");
  std::lock_guard<std::mutex> lock(mutex_);
  if(keys_.empty()||static_cast<uint32_t>(now-keys_at_)>=kKeysMs)return fail(error,"maintenance_trust_unavailable");
  Challenge *selected=nullptr;
  for(auto &c:challenges_)if(c.active&&c.state==state&&static_cast<uint32_t>(now-c.issued)<kChallengeMs){
    if(c.serial!=serial||c.uuid!=uuid||c.callback!=callback||c.opener!=opener)return fail(error,"maintenance_request_conflict");
    selected=&c;break;}
  if(!selected){for(auto &c:challenges_)if(!c.active||static_cast<uint32_t>(now-c.issued)>=kChallengeMs){selected=&c;break;}
    if(!selected)return fail(error,"maintenance_request_busy");
    *selected={serial,uuid,state,callback,opener,encode_hex(entropy),now,true};}
  out["protocol"]=2;out["serial"]=serial;out["device_uuid"]=uuid;out["state"]=state;out["challenge"]=selected->nonce;out["callback_origin"]=callback;
  out["expires"]=epoch+static_cast<int64_t>((kChallengeMs-static_cast<uint32_t>(now-selected->issued))/1000);
  error.clear();return true;
}
bool MaintenanceAuthority::exchange(const std::string &jwt,const std::string &state,const std::array<uint8_t,32> &entropy,uint32_t now,int64_t epoch,JsonObject out,std::string &error) {
  if(jwt.size()>4096||jwt.empty()||!hex(state,64)||epoch<1700000000||std::all_of(entropy.begin(),entropy.end(),[](uint8_t b){return b==0;}))return fail(error,"maintenance_token_invalid");
  const auto a=jwt.find('.'),b=jwt.find('.',a==std::string::npos?jwt.size():a+1);
  if(a==std::string::npos||b==std::string::npos||jwt.find('.',b+1)!=std::string::npos||a>512)return fail(error,"maintenance_token_invalid");
  std::vector<uint8_t> header_bytes,payload_bytes,signature;
  if(!base64url(std::string_view(jwt).substr(0,a),header_bytes)||!base64url(std::string_view(jwt).substr(a+1,b-a-1),payload_bytes)||!base64url(std::string_view(jwt).substr(b+1),signature))return fail(error,"maintenance_token_invalid");
  JsonDocument header,payload;
  if(deserializeJson(header,header_bytes.data(),header_bytes.size())||deserializeJson(payload,payload_bytes.data(),payload_bytes.size())||
      !header.is<JsonObjectConst>()||!payload.is<JsonObjectConst>()||!strings_safe(header.as<JsonVariantConst>())||!strings_safe(payload.as<JsonVariantConst>())||!exact_fields(header.as<JsonObjectConst>(),{"alg","typ","kid"})||
      std::string(header["alg"]|"")!="RS256"||std::string(header["typ"]|"")!=kMaintenanceType)return fail(error,"maintenance_token_invalid");
  std::lock_guard<std::mutex> lock(mutex_);
  if(keys_.empty()||static_cast<uint32_t>(now-keys_at_)>=kKeysMs)return fail(error,"maintenance_trust_unavailable");
  const std::string kid=header["kid"]|"";const Key *key=nullptr;
  for(const auto &candidate:keys_)if(candidate.kid==kid){key=&candidate;break;}
  if(!key)return fail(error,"maintenance_key_unavailable");
  digest::Sha256Value hash{};
  digest::RsaSha256Verifier verifier(key->der.data(),key->der.size(),key->bits);
  const bool verified=digest::sha256(reinterpret_cast<const uint8_t *>(jwt.data()),b,hash)&&verifier.verify(hash,signature.data(),signature.size());
  if(!verifier.close())return fail(error,"maintenance_key_retirement_pending");
  if(!verified)return fail(error,"maintenance_signature_invalid");
  if(!exact_fields(payload.as<JsonObjectConst>(),{"iss","aud","v","purpose","sub","serial","device_uuid","challenge","state","callback_origin","iat","exp","jti"})||
      std::string(payload["iss"]|"")!=kMaintenanceIssuer||std::string(payload["aud"]|"")!=kMaintenanceAudience||!payload["v"].is<int>()||payload["v"].as<int>()!=2||
      std::string(payload["purpose"]|"")!="device.maintenance"||!payload["iat"].is<int64_t>()||!payload["exp"].is<int64_t>())return fail(error,"maintenance_claims_invalid");
  const int64_t issued=payload["iat"],expires=payload["exp"];
  const std::string subject=payload["sub"]|"",jti=payload["jti"]|"";
  if(subject.empty()||subject.size()>128||!hex(jti,32)||issued<1700000000||issued>epoch+30||expires<=epoch||expires<=issued||expires-issued>120)return fail(error,"maintenance_token_expired");
  Challenge *challenge=nullptr;for(auto &c:challenges_)if(c.active&&c.state==state){challenge=&c;break;}
  if(!challenge||static_cast<uint32_t>(now-challenge->issued)>=kChallengeMs)return fail(error,"maintenance_challenge_expired");
  if(std::string(payload["state"]|"")!=state||std::string(payload["serial"]|"")!=challenge->serial||std::string(payload["device_uuid"]|"")!=challenge->uuid||
      std::string(payload["challenge"]|"")!=challenge->nonce||std::string(payload["callback_origin"]|"")!=challenge->callback)return fail(error,"maintenance_challenge_mismatch");
  Session *session=nullptr;for(auto &s:sessions_)if(s.value.empty()||static_cast<uint32_t>(now-s.issued)>=s.duration||s.expires<=epoch){session=&s;break;}
  if(!session)return fail(error,"maintenance_sessions_busy");
  session->value="lma2."+encode_hex(entropy);session->issued=now;session->duration=static_cast<uint32_t>(std::min<int64_t>(expires-epoch,120))*1000;
  session->expires=epoch+session->duration/1000;
  out["protocol"]=2;out["session"]=session->value;out["expires"]=session->expires;out["state"]=state;out["serial"]=challenge->serial;out["device_uuid"]=challenge->uuid;out["opener_origin"]=challenge->opener;
  *challenge={};error.clear();return true;
}
bool MaintenanceAuthority::authorized(const char *token,uint32_t now,int64_t epoch) const {
  if(!token||std::strlen(token)!=69||epoch<1700000000)return false;
  std::lock_guard<std::mutex> lock(mutex_);
  for(const auto &s:sessions_)if(s.expires>epoch&&static_cast<uint32_t>(now-s.issued)<s.duration&&equal(s.value,token))return true;
  return false;
}
void MaintenanceAuthority::revoke(const char *token) {
  std::lock_guard<std::mutex> lock(mutex_);for(auto &s:sessions_)if(equal(s.value,token))s={};
}
}
