#pragma once
#include <atomic>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
namespace smart_speaker::audio_internal {
inline constexpr size_t kBlockCount=8;
extern QueueHandle_t g_free_queue;
extern QueueHandle_t g_ready_queue;
extern SemaphoreHandle_t g_reconfigure_done;
extern SemaphoreHandle_t g_output_control_done;
extern TaskHandle_t g_output_task;
extern std::atomic<bool> g_output_stop;
extern std::atomic<bool> g_output_exited;
extern i2s_chan_handle_t g_left_tx;
extern i2s_chan_handle_t g_left_rx;
extern i2s_chan_handle_t g_right_tx;
bool prepare_audio_pipeline();
void reset_audio_control();
void output_task(void *);
uint32_t now_ms();
}
