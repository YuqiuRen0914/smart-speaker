#include "lunaredu_device_sdcard_test.h"

#include "sdkconfig.h"
#if CONFIG_LUNAREDU_DEVICE_STORAGE_SDCARD
#include "lunaredu_lunaros/esp_idf/device_test.h"
#include <esp_log.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>

namespace lunaredu::device::sdcard {
namespace {
constexpr const char *kTag = "test_sdcard";
bool owned = false;
std::string mount_path;
std::string temporary_path;
int file = -1;
[[maybe_unused]] uint32_t reported_ms = 0;
[[maybe_unused]] uint32_t phase = 0;

esp_err_t initialize(const void *opaque) {
  [[maybe_unused]] const auto &config = *static_cast<const TestConfig *>(opaque);
  reported_ms = 0;
  phase = 0;
  if (device_sdcard_storage_mounted()) return ESP_ERR_INVALID_STATE;
  mount_path = config.mount_path ? config.mount_path : "/sdcard";
  if (mount_path.empty() || mount_path[0] != '/' || mount_path.find("..") != std::string::npos) return ESP_ERR_INVALID_ARG;
  DeviceSdcardStorageConfig driver{true, mount_path.c_str()};
  owned = true;
  return device_sdcard_storage_mount(driver);
}

void tick([[maybe_unused]] uint32_t elapsed_ms) {
  if (!temporary_path.empty()) { ESP_LOGE(kTag, "temporary file cleanup pending; stop required"); return; }
  const std::string candidate = mount_path + "/.device-test-" + std::to_string(elapsed_ms) + "-" + std::to_string(phase++) + ".tmp";
  file = open(candidate.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  if (file < 0) { ESP_LOGE(kTag, "exclusive create failed"); return; }
  temporary_path = candidate;
  constexpr char expected[] = "LunarEdu SD card device test\n";
  char actual[sizeof(expected)] = {};
  bool ok = write(file, expected, sizeof(expected)) == static_cast<ssize_t>(sizeof(expected));
  ok = fsync(file) == 0 && ok;
  ok = lseek(file, 0, SEEK_SET) == 0 && ok;
  ok = read(file, actual, sizeof(actual)) == static_cast<ssize_t>(sizeof(actual)) && ok;
  ok = std::memcmp(expected, actual, sizeof(expected)) == 0 && ok;
  const int close_result = close(file); file = -1;
  const int remove_result = unlink(temporary_path.c_str());
  if (remove_result == 0) temporary_path.clear();
  const auto status = device_sdcard_storage_status();
  ESP_LOGI(kTag, "roundtrip=%d close=%d remove=%d capacity=%llu", ok, close_result, remove_result, static_cast<unsigned long long>(status.capacity_bytes));
}

esp_err_t shutdown() {
  if (file >= 0) { const int result = close(file); file = -1; if (result != 0) return ESP_FAIL; }
  if (!temporary_path.empty()) { if (unlink(temporary_path.c_str()) != 0) return ESP_FAIL; temporary_path.clear(); }
  if (owned && device_sdcard_storage_mounted()) {
    const auto result = device_sdcard_storage_unmount();
    if (result != ESP_OK) return result;
  }
  owned = false;
  mount_path.clear();
  return ESP_OK;
}

const lunaros::esp_idf::DeviceTest<initialize, tick, shutdown> definition{kTag, 3000};
}  // namespace

esp_err_t start_test(const TestConfig &config) { return definition.start(&config); }
esp_err_t stop_test() { return definition.stop(); }

}  // namespace lunaredu::device::sdcard
#else
namespace lunaredu::device::sdcard {
esp_err_t start_test(const TestConfig &) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t stop_test() { return ESP_OK; }
}  // namespace lunaredu::device::sdcard
#endif
