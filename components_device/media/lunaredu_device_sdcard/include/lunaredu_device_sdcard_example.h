#pragma once
#include <esp_err.h>

namespace lunaredu::device::sdcard {
// Native SDMMC one-bit wiring; D1/D2/D3 are unused.
struct Wiring { int clk = -1, cmd = -1, d0 = -1; };
// Blocking minimum test: default parameters, about ten seconds, then cleanup.
// Call from an ESP-IDF task with exclusive access to this peripheral.
esp_err_t example(const Wiring &wiring);
} // namespace lunaredu::device::sdcard
