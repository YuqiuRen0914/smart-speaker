#include "lunaredu_lunaros/network/network_job_pump.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace lunaredu::lunaros {
namespace {
void wake(void *task) { xTaskNotifyGive(static_cast<TaskHandle_t>(task)); }
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
} // namespace
void enter_network_job_pump(void *) {
  set_network_job_wake_handler(wake, xTaskGetCurrentTaskHandle());
}
void leave_network_job_pump(void *) {
  set_network_job_wake_handler(nullptr, nullptr);
}
uint32_t poll_network_job_pump(void *) {
  (void)run_next_network_job(now_ms());
  return next_network_job_wake_delay_ms(now_ms());
}
} // namespace lunaredu::lunaros
