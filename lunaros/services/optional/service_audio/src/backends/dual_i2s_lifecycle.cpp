#include "dual_i2s_internal.h"
#include "kernel/managed_task.h"
#include "kernel/runtime.h"
#include <cstring>
#include <mutex>
namespace lunaredu::audio {
namespace audio_internal {
QueueHandle_t g_free_queue = nullptr;
QueueHandle_t g_ready_queue = nullptr;
SemaphoreHandle_t g_reconfigure_done = nullptr;
SemaphoreHandle_t g_output_control_done = nullptr;
TaskHandle_t g_output_task = nullptr;
std::atomic<bool> g_output_stop{false};
std::atomic<bool> g_output_exited{true};
std::atomic<bool> control_wake_pending{false};
// Setters may arrive while the service retires. Keep their borrowed queue valid
// through enqueue; the worker itself is acknowledged before queue destruction.
std::mutex control_queue_mutex;
void wake_output_control(){
  std::lock_guard<std::mutex> lock(control_queue_mutex);
  if(!g_ready_queue||control_wake_pending.exchange(true))return;
  const uint8_t token=kControlWake;
  if(xQueueSend(g_ready_queue,&token,0)!=pdTRUE)control_wake_pending=false;
}
bool consume_output_control(uint8_t index){
  if(index!=kControlWake)return false;
  control_wake_pending=false;return true;
}
i2s_chan_handle_t g_left_tx = nullptr;
i2s_chan_handle_t g_right_tx = nullptr;
}
using namespace audio_internal;
bool stop_dual_i2s_audio(lunaredu::lunaros::KernelRuntime &runtime);
bool setup_dual_i2s_audio(lunaredu::lunaros::KernelRuntime &runtime, const DualI2sConfig &config) {
  if(!valid_dual_i2s_config(config))return false;
  if (g_output_task && !g_output_stop.load()) {
    return config.left.port==g_config.left.port&&config.right.port==g_config.right.port&&
      config.left.bclk==g_config.left.bclk&&config.left.ws==g_config.left.ws&&config.left.dout==g_config.left.dout&&
      config.right.bclk==g_config.right.bclk&&config.right.ws==g_config.right.ws&&config.right.dout==g_config.right.dout&&
      config.fade_ms==g_config.fade_ms&&config.single_output==g_config.single_output&&!strcmp(config.settings_namespace,g_config.settings_namespace)&&
      !strcmp(config.task_id,g_config.task_id)&&!strcmp(config.service_id,g_config.service_id);
  }
  if (!stop_dual_i2s_audio(runtime)) return false;
  if (!valid_dual_i2s_config(config)) return false;
  g_config=config;
  struct Rollback {
    lunaredu::lunaros::KernelRuntime &runtime;
    bool complete = false;
    ~Rollback() { if (!complete) (void)stop_dual_i2s_audio(runtime); }
  } rollback{runtime};
  g_free_queue = xQueueCreate(kBlockCount, sizeof(uint8_t));
  {
    std::lock_guard<std::mutex> lock(control_queue_mutex);
    control_wake_pending=false;
    g_ready_queue = xQueueCreate(kBlockCount+1, sizeof(uint8_t));
  }
  g_reconfigure_done = xSemaphoreCreateBinary();
  g_output_control_done = xSemaphoreCreateBinary();
  if (!g_free_queue || !g_ready_queue || !g_reconfigure_done ||
      !g_output_control_done)
    return false;
  for (uint8_t i = 0; i < kBlockCount; ++i)
    (void)xQueueSend(g_free_queue, &i, 0);
  if (!prepare_audio_pipeline()) return false;
  std::string task_error;
  g_output_stop.store(false);
  g_output_exited.store(false);
  if (!lunaredu::lunaros::create_managed_task(
      runtime,
      {.id = g_config.task_id, .owner_service_id = g_config.service_id, .role = "audio_output",
       .stack_bytes = 8192, .priority = 18, .core_affinity = 0,
       .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Resident,
       .memory = lunaredu::lunaros::ManagedTaskMemory::Internal},
      output_task, nullptr, &g_output_task, task_error)) {
    g_output_exited.store(true);
    return false;
  }
  rollback.complete = true;
  return true;
}

bool stop_dual_i2s_audio(lunaredu::lunaros::KernelRuntime &runtime) {
  g_output_stop.store(true, std::memory_order_release);
  wake_output_control();
  if (g_output_task) {
    const uint32_t started = now_ms();
    while (!g_output_exited.load(std::memory_order_acquire)) {
      if (now_ms() - started >= 1000) return false;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    while (runtime.task_active(g_config.task_id,lunaredu::lunaros::ManagedTaskOwnerKind::Service,g_config.service_id)) {
      if(now_ms()-started>=1000) return false;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    g_output_task = nullptr;
  }
  if (!close_audio_pipeline()) return false;
  bool released = true;
  for (auto *handle : {&g_right_tx, &g_left_tx}) {
    if (!*handle) continue;
    const auto stopped = i2s_channel_disable(*handle);
    if (stopped != ESP_OK && stopped != ESP_ERR_INVALID_STATE) { released = false; continue; }
    if (i2s_del_channel(*handle) != ESP_OK) { released = false; continue; }
    *handle = nullptr;
  }
  if (!released) return false;
  {
    std::lock_guard<std::mutex> lock(control_queue_mutex);
    for (auto *queue : {&g_ready_queue, &g_free_queue}) {
      if (*queue) { vQueueDelete(*queue); *queue = nullptr; }
    }
  }
  for (auto *semaphore : {&g_output_control_done, &g_reconfigure_done}) {
    if (*semaphore) { vSemaphoreDelete(*semaphore); *semaphore = nullptr; }
  }
  reset_audio_control();
  return true;
}
}
