#pragma once

#include <esp_err.h>
#include "lunaredu_device_i2s_audio_config.h"

namespace lunaredu::device::i2s_audio {

using TestConfig = DeviceI2sAudioConfig;

// 麦克风输出峰值与读取统计，扬声器每两秒播放低幅度 440Hz 短音
// Nonblocking after initialization. Do not run the product driver concurrently.
// Borrowed BSP/bus pointers must outlive stop_test(). Config itself is copied.
esp_err_t start_test(const TestConfig &config);
// Waits for in-flight test I/O, stops outputs and releases owned resources.
esp_err_t stop_test();

// Pin/bus convenience entry: no application polling or task setup required.
inline esp_err_t start_test(int bclk_gpio, int ws_gpio, int dout_gpio, int din_gpio = -1) {
  TestConfig config; config.duplex.bclk = bclk_gpio; config.duplex.ws = ws_gpio; config.duplex.dout = dout_gpio; config.duplex.din = din_gpio;
  return start_test(config);
}

}  // namespace lunaredu::device::i2s_audio
