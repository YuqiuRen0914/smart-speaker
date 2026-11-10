#pragma once
#include "FreeRTOS.h"
using SemaphoreHandle_t = bool *;
inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  ++runtime_test::sync_allocations; return new bool(false);
}
inline void vSemaphoreDelete(SemaphoreHandle_t s) { --runtime_test::sync_allocations; delete s; }
inline int xSemaphoreGive(SemaphoreHandle_t s) { *s = true; return pdTRUE; }
inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t timeout) {
  if (timeout) runtime_test::pump();
  if (*s) { *s = false; return pdTRUE; }
  runtime_test::time_us += timeout * 1000;
  return pdFALSE;
}
