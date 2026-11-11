#pragma once

#include "product_state.h"

namespace smart_speaker::product {

ProductSnapshot product_state_snapshot(uint32_t monotonic_seconds, uint32_t now_ms);
NavigationSnapshot product_navigation_snapshot();
std::string product_pending_reminder_hint();
bool product_time_load(uint32_t monotonic_seconds = 0);
bool product_time_save(uint32_t monotonic_seconds, uint32_t now_ms);
size_t product_tick_time(uint32_t monotonic_seconds, int64_t epoch_seconds,
                         bool time_trusted, bool conversation_active);
bool product_navigation_input(Input input);
bool product_pomodoro_start(uint32_t monotonic_seconds);
bool product_pomodoro_pause(uint32_t monotonic_seconds);
bool product_pomodoro_resume(uint32_t monotonic_seconds);
bool product_pomodoro_finish();
enum class PomodoroAction : uint8_t { None, Start, Pause, Resume, Finish };
bool product_request_pomodoro(PomodoroAction action);
void product_poll_pomodoro_request(uint32_t monotonic_seconds);
uint32_t product_add_reminder(int64_t due_epoch_seconds, ReminderRepeat repeat, std::string title);
bool product_update_reminder(uint32_t id, int64_t due_epoch_seconds, ReminderRepeat repeat, std::string title);
bool product_remove_reminder(uint32_t id);
bool product_handle_reminder(uint32_t id);
bool product_pair_computer(std::string computer_id);
bool product_disconnect_computer(const std::string &computer_id);
void product_clear_computer();
bool product_update_computer(const std::string &computer_id, const ComputerTelemetry &sample, uint32_t now_ms);
void product_hit_wood_fish(uint32_t now_ms);
bool product_move_2048(int dx, int dy);
void product_reset_2048();

}  // namespace smart_speaker::product
