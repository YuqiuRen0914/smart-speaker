#pragma once

#include <esp_err.h>
#include "lunaredu_device_sdcard_storage.h"

namespace lunaredu::device::sdcard {

using TestConfig = DeviceSdcardStorageConfig;

// 通过当前 BSP 挂载 SD 卡，每三秒创建独占临时文件并核对写入、读回和删除结果
// Nonblocking after initialization. Do not run the product driver concurrently.
// Borrowed BSP/bus pointers must outlive stop_test(). Config itself is copied.
esp_err_t start_test(const TestConfig &config = {});
// Waits for in-flight test I/O, stops outputs and releases owned resources.
esp_err_t stop_test();

}  // namespace lunaredu::device::sdcard
