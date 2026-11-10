#include "audio_output_internal.h"
#include "lunaredu_lunaros/kernel/managed_task.h"
#include <cstring>
namespace smart_speaker {
namespace audio_internal {
QueueHandle_t g_free_queue = nullptr;
QueueHandle_t g_ready_queue = nullptr;
SemaphoreHandle_t g_reconfigure_done = nullptr;
SemaphoreHandle_t g_output_control_done = nullptr;
TaskHandle_t g_output_task = nullptr;
std::atomic<bool> g_output_stop{false};
std::atomic<bool> g_output_exited{true};
i2s_chan_handle_t g_left_tx = nullptr;
i2s_chan_handle_t g_left_rx = nullptr;
i2s_chan_handle_t g_right_tx = nullptr;
}
using namespace audio_internal;
bool stop_dual_i2s_audio();
bool setup_dual_i2s_audio() {
  if (g_output_task && !g_output_stop.load()) return true;
  if (!stop_dual_i2s_audio()) return false;
  struct Rollback {
    bool complete = false;
    ~Rollback() { if (!complete) (void)stop_dual_i2s_audio(); }
  } rollback;
  g_free_queue = xQueueCreate(kBlockCount, sizeof(uint8_t));
  g_ready_queue = xQueueCreate(kBlockCount, sizeof(uint8_t));
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
      {.id = "speaker_i2s", .owner_service_id = "speaker.audio", .role = "audio_output",
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

bool stop_dual_i2s_audio() {
  g_output_stop.store(true, std::memory_order_release);
  if (g_output_task) {
    const uint32_t started = now_ms();
    while (!g_output_exited.load(std::memory_order_acquire)) {
      if (now_ms() - started >= 1000) return false;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    for (;;) {
      const auto registry=lunaredu::lunaros::managed_task_registry_snapshot();
      bool registered=false;
      for(const auto &task:registry.tasks)
        if(task.active && strcmp(task.id.data(),"speaker_i2s")==0) registered=true;
      if(!registered) break;
      if(now_ms()-started>=1000) return false;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    g_output_task = nullptr;
  }
  bool released = true;
  for (auto *handle : {&g_right_tx, &g_left_rx, &g_left_tx}) {
    if (!*handle) continue;
    const auto stopped = i2s_channel_disable(*handle);
    if (stopped != ESP_OK && stopped != ESP_ERR_INVALID_STATE) { released = false; continue; }
    if (i2s_del_channel(*handle) != ESP_OK) { released = false; continue; }
    *handle = nullptr;
  }
  if (!released) return false;
  for (auto *queue : {&g_ready_queue, &g_free_queue}) {
    if (*queue) { vQueueDelete(*queue); *queue = nullptr; }
  }
  for (auto *semaphore : {&g_output_control_done, &g_reconfigure_done}) {
    if (*semaphore) { vSemaphoreDelete(*semaphore); *semaphore = nullptr; }
  }
  reset_audio_control();
  return true;
}
}
