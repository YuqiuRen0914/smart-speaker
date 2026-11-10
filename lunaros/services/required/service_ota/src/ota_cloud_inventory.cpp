#include "lunaredu_ota_cloud.h"
#include "lunaredu_ota.h"
#ifdef ESP_PLATFORM
#include "ota_sdk_verification.h"
#include "ota_verification_retry.h"
#include "ota_writer.h"
#include <sdkconfig.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#endif

namespace lunaredu::lunaros {
#ifdef ESP_PLATFORM
namespace {
bool digest(const std::string &value) {
  return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c) {
    return (c>='0'&&c<='9')||(c>='a'&&c<='f');
  });
}
std::string hex(const uint8_t *data,size_t size) {
  constexpr char alphabet[]="0123456789abcdef";
  std::string result;
  result.reserve(size*2);
  for(size_t index=0;index<size;++index) {
    result+=alphabet[data[index]>>4];result+=alphabet[data[index]&15];
  }
  return result;
}
struct VerifiedImage {
  uint32_t address=0;
  std::string elf,generation;
  bool verified=false;
  esp_err_t verification_error=ESP_OK;
  OtaVerificationRetry retry;
};
}
#endif

OtaCloudInventory ota_service_cloud_inventory() {
  OtaCloudInventory out;
#ifdef ESP_PLATFORM
  const auto metadata=ota_writer_metadata();
  const auto status=ota_service_status_snapshot();
  const auto deployment=ota_service_deployment_snapshot();
  // Legacy/unvalidated layouts cannot advertise a role protocol.
  if(!deployment.ready || (std::strcmp(deployment.mode,"dual_slot")!=0 &&
      std::strcmp(deployment.mode,"recovery")!=0))return out;
  out.layout_id=deployment.layout_id;
#ifdef CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE
  out.hardware_id=CONFIG_LUNAREDU_OTA_RECOVERY_HARDWARE;
#elif defined(CONFIG_LUNAROS_BOARD_TYPE)
  out.hardware_id=CONFIG_LUNAROS_BOARD_TYPE;
#endif
  out.running_role=metadata.project_name=="lunaros-bootstrap"?"boot":"application";
  out.ready=status.boot_confirmed&&!status.pending_verify&&!status.upload_active;
  if(!out.ready)return out;
  if(ota_sdk_image_write_active().load()){out.ready=false;return out;}
  static std::mutex mutex;
  static VerifiedImage cache[2];
  std::lock_guard<std::mutex> lock(mutex);
  // Recheck after every owner journal boundary, even if an interrupted rewrite
  // retained the old descriptor/ELF. Presence never hashes complete BIN files.
  const auto generation=std::to_string(status.install_id)+":"+status.install_phase+":"+
      std::to_string(status.install_target_address)+":"+
      std::to_string(ota_sdk_image_generation().load());
  const esp_partition_t *partitions[2]{};
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  partitions[0]=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,"recovery");
  partitions[1]=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,"ota_0");
#else
  partitions[0]=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,"ota_0");
  partitions[1]=esp_partition_find_first(ESP_PARTITION_TYPE_APP,static_cast<esp_partition_subtype_t>(17),"ota_1");
#endif
  for(size_t index=0;index<2;++index) {
    const auto *partition=partitions[index];
    esp_app_desc_t desc{};
    esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
    if(!partition||esp_ota_get_partition_description(partition,&desc)!=ESP_OK||
        !std::memchr(desc.project_name,0,sizeof(desc.project_name))||
        !std::memchr(desc.version,0,sizeof(desc.version)))continue;
    const bool confirmed=partition->subtype==ESP_PARTITION_SUBTYPE_APP_FACTORY||
        (esp_ota_get_state_partition(partition,&state)==ESP_OK&&state==ESP_OTA_IMG_VALID);
    if(!confirmed){cache[index]={};continue;}
    const auto elf=hex(desc.app_elf_sha256,sizeof(desc.app_elf_sha256));
    auto &entry=cache[index];
    if(entry.address!=partition->address||entry.elf!=elf||entry.generation!=generation||entry.retry.due(ota_verification_now_ms())) {
      std::array<uint8_t,368> prefix{};
      OtaCloudInstallRequest identity;
      identity.target_role=std::strcmp(desc.project_name,"lunaros-bootstrap")==0?"boot":"application";
      identity.layout_id=out.layout_id;identity.hardware_id=out.hardware_id;
      identity.project_name=desc.project_name;identity.version=desc.version;identity.app_elf_sha256=elf;
      std::string error;
      esp_image_metadata_t parsed{};
      const esp_partition_pos_t position{partition->address,partition->size};
      const esp_err_t read_result=esp_partition_read(partition,0,prefix.data(),prefix.size());
      const bool prefix_valid=read_result==ESP_OK&&
          ota_cloud_prefix_allowed(identity,prefix.data(),prefix.size(),error);
      const esp_err_t result=prefix_valid?ota_sdk_verify_image(ESP_IMAGE_VERIFY,&position,&parsed):read_result;
      const bool valid=prefix_valid&&result==ESP_OK&&parsed.image_len>0&&parsed.image_len<=partition->size;
      entry={partition->address,elf,generation,valid,result,{}};
      entry.retry.record(result!=ESP_OK&&result!=ESP_ERR_IMAGE_INVALID,
                         ota_verification_now_ms());
    }
    if(!entry.verified)continue;
    auto &image=std::strcmp(desc.project_name,"lunaros-bootstrap")==0?out.boot:out.application;
    image.present=image.confirmed=true;
    image.project_name=desc.project_name;image.version=desc.version;image.app_elf_sha256=elf;
    // SDK image digest is not the hash of the complete transport BIN. Only the
    // OTA owner's verified transfer journal can provide this optional field.
    if(status.journal_error.empty()&&status.install_target_address==partition->address&&
        status.install_elf_sha256==elf&&
        (status.install_phase=="confirmed"||status.install_phase=="boot_updated")&&
        digest(status.install_sha256))image.sha256=status.install_sha256;
  }
  const auto &running=out.running_role=="boot"?out.boot:out.application;
  out.ready=running.present&&running.confirmed;
#endif
  return out;
}
}
