#pragma once
#include <cstdint>
using TaskHandle_t = void *;
using TickType_t = uint32_t;
constexpr int pdTRUE = 1;
constexpr uint32_t pdMS_TO_TICKS(uint32_t value) { return value; }
