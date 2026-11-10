#include "kernel/task_signal.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace lunaredu::lunaros::task_signal {
namespace {
constexpr UBaseType_t kWakeIndex = 1;
static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES > kWakeIndex,
              "LunarOS requires a notification slot separate from SDK drivers");
}
void notify(void *task) {
  if(task)xTaskNotifyGiveIndexed(static_cast<TaskHandle_t>(task),kWakeIndex);
}
uint32_t wait_ms(uint32_t timeout_ms) {
  const TickType_t ticks=timeout_ms==kWaitForever?portMAX_DELAY:
      timeout_ms==0?0:pdMS_TO_TICKS(timeout_ms)>0?pdMS_TO_TICKS(timeout_ms):1;
  return ulTaskNotifyTakeIndexed(kWakeIndex,pdTRUE,ticks);
}
}
