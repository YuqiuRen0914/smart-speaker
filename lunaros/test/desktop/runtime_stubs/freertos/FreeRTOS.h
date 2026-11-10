#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
struct tskTaskControlBlock;
using TaskHandle_t = tskTaskControlBlock *;
using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
constexpr int pdTRUE = 1, pdFALSE = 0;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
namespace runtime_test {
inline int64_t time_us = 1000;
inline bool worker_blocked = false, reject_queue = false, fail_worker = false;
inline void (*worker)(void *) = nullptr;
inline int workers_started = 0, restarts = 0, sync_allocations = 0;
struct WorkerIdle {};
struct Restart {};
inline void pump() {
  if (worker_blocked || !worker) return;
  try { worker(nullptr); } catch (const WorkerIdle &) {}
}
}
inline void vTaskDelay(TickType_t n) { runtime_test::time_us += n * 1000; }
