#pragma once

// ESP-IDF component integration only. OS domain contracts use device_test.h.
#include <esp_err.h>
#include "lunaredu_lunaros/kernel/device_test.h"

namespace lunaredu::lunaros::esp_idf {
inline DeviceTestResult device_test_result(esp_err_t error) {
  switch (error) {
  case ESP_OK: return DeviceTestResult::Ok;
  case ESP_ERR_INVALID_ARG: return DeviceTestResult::InvalidArgument;
  case ESP_ERR_INVALID_STATE: return DeviceTestResult::InvalidState;
  case ESP_ERR_NO_MEM: return DeviceTestResult::NoMemory;
  case ESP_ERR_NOT_SUPPORTED: return DeviceTestResult::NotSupported;
  case ESP_ERR_TIMEOUT: return DeviceTestResult::Timeout;
  default: return DeviceTestResult::Failed;
  }
}
inline esp_err_t device_test_error(DeviceTestResult result) {
  switch (result) {
  case DeviceTestResult::Ok: return ESP_OK;
  case DeviceTestResult::InvalidArgument: return ESP_ERR_INVALID_ARG;
  case DeviceTestResult::InvalidState: return ESP_ERR_INVALID_STATE;
  case DeviceTestResult::NoMemory: return ESP_ERR_NO_MEM;
  case DeviceTestResult::NotSupported: return ESP_ERR_NOT_SUPPORTED;
  case DeviceTestResult::Timeout: return ESP_ERR_TIMEOUT;
  default: return ESP_FAIL;
  }
}

// Keep callback-specific native errors here, never in the OS contract. Core
// lifecycle callbacks execute synchronously on the start/stop caller. Separate
// thread-local slots preserve the init error even if its rollback also fails.
template<esp_err_t (*Initialize)(const void *), void (*Tick)(uint32_t),
         esp_err_t (*Shutdown)()>
class DeviceTest {
  inline static thread_local esp_err_t initialize_error_ = ESP_OK;
  inline static thread_local esp_err_t shutdown_error_ = ESP_OK;
  static DeviceTestResult initialize(const void *config) {
    initialize_error_ = Initialize(config);
    return device_test_result(initialize_error_);
  }
  static DeviceTestResult shutdown() {
    shutdown_error_ = Shutdown();
    return device_test_result(shutdown_error_);
  }
  const DeviceTestDefinition definition_;
public:
  constexpr DeviceTest(const char *name, uint32_t interval_ms)
      : definition_{name, initialize, Tick, shutdown, interval_ms} {}
  esp_err_t start(const void *config) const {
    initialize_error_ = ESP_OK;
    const auto result = start_device_test(definition_, config);
    return initialize_error_ != ESP_OK ? initialize_error_ : device_test_error(result);
  }
  esp_err_t stop() const {
    shutdown_error_ = ESP_OK;
    const auto result = stop_device_test(definition_);
    return shutdown_error_ != ESP_OK ? shutdown_error_ : device_test_error(result);
  }
};
} // namespace lunaredu::lunaros::esp_idf
