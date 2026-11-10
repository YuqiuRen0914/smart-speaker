#include "system/system_info.h"

#include <esp_flash.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>

namespace lunaredu::system {

namespace {

constexpr const char *kTag = "SystemInfo";

} // namespace

size_t SystemInfo::GetFlashSize() {
  uint32_t flash_size = 0;
  if (esp_flash_get_size(nullptr, &flash_size) != ESP_OK) {
    ESP_LOGE(kTag, "Failed to get flash size");
    return 0;
  }
  return static_cast<size_t>(flash_size);
}

size_t SystemInfo::GetMinimumFreeHeapSize() {
  return esp_get_minimum_free_heap_size();
}

std::string SystemInfo::GetMacAddress() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(mac_str);
}

std::string SystemInfo::GetChipModelName() {
  return std::string(CONFIG_IDF_TARGET);
}

} // namespace lunaredu::system
