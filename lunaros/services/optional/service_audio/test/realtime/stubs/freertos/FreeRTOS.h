#pragma once
#include <cstddef>
#include <cstdint>
using BaseType_t=int;
using UBaseType_t=unsigned;
using TickType_t=uint32_t;
struct tskTaskControlBlock;
using TaskHandle_t=tskTaskControlBlock *;
struct AudioTestQueue;
using QueueHandle_t=AudioTestQueue *;
inline constexpr BaseType_t pdTRUE=1,pdFALSE=0;
inline constexpr TickType_t portMAX_DELAY=UINT32_MAX;
inline constexpr unsigned configTASK_NOTIFICATION_ARRAY_ENTRIES=2;
inline constexpr TickType_t pdMS_TO_TICKS(uint32_t ms){return ms;}
