#include "lunaredu_nvs.h"
#include <esp_err.h>
#include <mutex>
#include <nvs_flash.h>

namespace lunaredu::nvs {
bool initialize(std::string &error) {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  const esp_err_t result = nvs_flash_init();
  error = result == ESP_OK ? "" : esp_err_to_name(result);
  return result == ESP_OK;
}
} // namespace lunaredu::nvs
