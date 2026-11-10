#include "storage/nvs_backend.h"

#include <esp_err.h>
#include <mutex>
#include <nvs_flash.h>
#if CONFIG_NVS_ENCRYPTION
#include <esp_partition.h>
#include <mbedtls/platform_util.h>
#endif

namespace lunaredu::lunaros {

bool initialize_nvs_partition(const char *partition, std::string &error) {
  if (!partition || !*partition) { error = "invalid_nvs_partition"; return false; }
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
#if CONFIG_NVS_ENCRYPTION
  // Independent NVS partitions must use the same provisioned NVS key set.
  // The ordinary SDK init_partition path would silently initialize plaintext.
  const auto *keys=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,"nvs_keys");
  if(!keys){error="nvs_encryption_keys_missing";return false;}
  nvs_sec_cfg_t security{};
  esp_err_t result=nvs_flash_read_security_cfg(keys,&security);
  if(result==ESP_OK)result=nvs_flash_secure_init_partition(partition,&security);
  mbedtls_platform_zeroize(&security,sizeof(security));
#else
  const esp_err_t result = nvs_flash_init_partition(partition);
#endif
  error = result == ESP_OK ? "" : esp_err_to_name(result);
  return result == ESP_OK;
}

bool initialize_nvs(std::string &error) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  const esp_err_t result = nvs_flash_init();
  error = result == ESP_OK ? "" : esp_err_to_name(result);
  return result == ESP_OK;
}

} // namespace lunaredu::lunaros
