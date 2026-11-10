#include "app/maintenance_io.h"
#include "lunaredu_lunaros/app/maintenance.h"
#include "lunaredu_lunaros/kernel/managed_task.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/network/network_job_pump.h"
#include <algorithm>
#include <atomic>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::lunaros {
namespace {
enum class IoStartState { Idle, Starting, Ready, Failed };
std::atomic<IoStartState> io_state{IoStartState::Idle};
std::atomic<uint32_t> io_heartbeat{0};
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
void io_task(void *) {
  std::string error;
  if (!bind_service_executor_to_current_task(kNetworkIoServiceExecutor, &error)) {
    io_state.store(IoStartState::Failed, std::memory_order_release);
    // Returning lets the managed-task wrapper release the task admission.
    return;
  }
  enter_network_job_pump(nullptr);
  io_state.store(IoStartState::Ready, std::memory_order_release);
  for (;;) {
    pump_service_executor(kNetworkIoServiceExecutor, 0);
    const auto next_delay = poll_network_job_pump(nullptr);
    io_heartbeat.store(now_ms());
    const auto delay = std::clamp<uint32_t>(next_delay, 1, 100);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay));
  }
}
}
bool start_maintenance_io(std::string &error) {
  error.clear();
  auto expected = IoStartState::Idle;
  if (io_state.compare_exchange_strong(expected, IoStartState::Starting)) {
    if (!create_managed_task({.id = "maintenance_io", .owner_service_id = kNetworkRuntimeServiceId,
      .role = "blocking_io", .stack_bytes = 12288, .priority = 1, .core_affinity = 0,
      .lifetime = ManagedTaskLifetime::Resident, .memory = ManagedTaskMemory::Internal},
      io_task, nullptr, nullptr, error)) {
      io_state.store(IoStartState::Idle, std::memory_order_release);
      return false;
    }
  }
  const auto started = now_ms();
  for (;;) {
    const auto state = io_state.load(std::memory_order_acquire);
    if (state == IoStartState::Ready) return true;
    if (state == IoStartState::Failed) {
      auto failed = IoStartState::Failed;
      (void)io_state.compare_exchange_strong(failed, IoStartState::Idle);
      error = "maintenance_io_bind_failed";
      return false;
    }
    // A timeout bounds this wait, not the lifetime of the task. A retry joins
    // the same starting task; it must neither duplicate it nor retain a stale
    // task handle. A reported bind failure allows a fresh managed admission.
    if (now_ms() - started > 5000) {
      error = "maintenance_io_timeout";
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
bool maintenance_io_healthy() {
  return io_state.load(std::memory_order_acquire) == IoStartState::Ready &&
      now_ms() - io_heartbeat.load() < 1000;
}
} // namespace lunaredu::lunaros
