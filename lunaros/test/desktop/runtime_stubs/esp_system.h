#pragma once
#include "freertos/FreeRTOS.h"
[[noreturn]] inline void esp_restart() {
  ++runtime_test::restarts;
  throw runtime_test::Restart{};
}
