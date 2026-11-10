#pragma once

#include <esp_err.h>
#include "lunaredu_device_spi_lcd.h"

namespace lunaredu::device::spi_lcd {

using TestConfig = DeviceSpiLcdConfig;

// 逐行刷新红、绿、蓝、白、黑测试画面，支持现有 SPI 屏幕驱动
// Nonblocking after initialization. Do not run the product driver concurrently.
// Borrowed BSP/bus pointers must outlive stop_test(). Config itself is copied.
esp_err_t start_test(const TestConfig &config);
// Waits for in-flight test I/O, stops outputs and releases owned resources.
esp_err_t stop_test();

// Pin/bus convenience entry: no application polling or task setup required.
inline esp_err_t start_test(int mosi_gpio, int sclk_gpio, int cs_gpio, int dc_gpio) {
  TestConfig config; config.mosi_gpio = mosi_gpio; config.sclk_gpio = sclk_gpio; config.cs_gpio = cs_gpio; config.dc_gpio = dc_gpio;
  config.claim_bsp_spi_resource = false;
  return start_test(config);
}

}  // namespace lunaredu::device::spi_lcd
