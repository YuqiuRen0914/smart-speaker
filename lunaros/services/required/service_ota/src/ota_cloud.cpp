#include "lunaredu_ota_cloud.h"
#include "lunaredu_ota.h"
#include "ota_image_policy.h"
#include <algorithm>
#include <cstring>


namespace lunaredu::lunaros {
namespace {
bool digest(const std::string &s) {
  return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
std::string hex(const uint8_t *data,size_t size) {
  constexpr char alphabet[]="0123456789abcdef";std::string value;value.reserve(size*2);
  for(size_t i=0;i<size;++i){value+=alphabet[data[i]>>4];value+=alphabet[data[i]&15];}return value;
}
bool fail(const char *reason,std::string &error){error=reason;return false;}
}
bool ota_cloud_request_allowed(const OtaCloudInstallRequest &r,const OtaCloudInventory &inventory,std::string &error) {
  if(r.protocol_version!=2 || (r.target_role!="boot"&&r.target_role!="application") ||
      r.url.rfind("https://",0)!=0 || r.url.size()>1024 || r.url.find('\0')!=std::string::npos ||
      !digest(r.sha256)||!digest(r.app_elf_sha256)||r.project_name.empty()||r.project_name.size()>31||
      r.version.empty()||r.version.size()>31 || r.hardware_id.empty()||r.hardware_id.size()>31 ||
      (r.layout_id!="lunaros-16m-v2"&&!ota_cloud_recovery_layout(r.layout_id)) ||
      ((r.project_name=="lunaros-bootstrap")!=(r.target_role=="boot"))) return fail("ota_cloud_identity_invalid",error);
  if(!inventory.ready || r.layout_id!=inventory.layout_id || r.hardware_id!=inventory.hardware_id)
    return fail("ota_cloud_deployment_mismatch",error);
  const size_t capacity=r.target_role=="boot"&&ota_cloud_recovery_layout(r.layout_id)?0x200000:0x600000;
  if(!r.size || r.size>capacity)return fail("ota_image_exceeds_partition",error);
  const auto &current=r.target_role=="boot"?inventory.boot:inventory.application;
  if(current.present&&current.confirmed){
    const OtaCandidateIdentity previous{current.project_name,current.version,0},candidate{r.project_name,r.version,0};
    const bool same=current.app_elf_sha256==r.app_elf_sha256;
    if(r.target_role=="boot"){
      if(!ota_boot_candidate_identity_allowed(previous,candidate,error,same))return false;
    }else if(!(same&&current.version==r.version)&&!ota_candidate_identity_allowed(previous,candidate,error,false))return false;
  }
  error.clear();return true;
}
bool ota_cloud_prefix_allowed(const OtaCloudInstallRequest &r,const uint8_t *data,size_t size,std::string &error){
  if(!data||size<368||data[0]!=0xe9||std::memcmp(data+288,"LUNARV3\0",8))return fail("ota_cloud_prefix_invalid",error);
  const auto u32=[data](size_t at){return uint32_t(data[at])|(uint32_t(data[at+1])<<8)|(uint32_t(data[at+2])<<16)|(uint32_t(data[at+3])<<24);};
  const auto string=[data](size_t at){const char *value=reinterpret_cast<const char *>(data+at);const auto *end=static_cast<const char *>(std::memchr(value,0,32));return end?std::string(value,size_t(end-value)):std::string{};};
  const uint32_t role=r.target_role=="boot"&&ota_cloud_recovery_layout(r.layout_id)?1U:2U;
  if(u32(32)!=0xabcd5432||u32(296)!=1||u32(300)!=role||string(304)!=r.layout_id||string(336)!=r.hardware_id||
      string(80)!=r.project_name||string(48)!=r.version||hex(data+176,32)!=r.app_elf_sha256)return fail("ota_cloud_candidate_mismatch",error);
  error.clear();return true;
}

}
