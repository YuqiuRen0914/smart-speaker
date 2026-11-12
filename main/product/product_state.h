#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace smart_speaker::product {

// Product state is deliberately independent from LVGL, HTTP and audio
// drivers.  The device page, web projection and assistant tools can all read
// and mutate this one owner without keeping synchronised copies.
enum class Page : uint8_t { Desktop, ModuleList, ModuleMenu, Feature, Settings, PetActions };
enum class Module : uint8_t { Music, Assistant, Time, Information, Games, Settings };
enum class Input : uint8_t { Up, Down, Left, Right, Confirm, Back, Home, Assistant };

struct NavigationSnapshot {
  Page page = Page::Desktop;
  Module module = Module::Music;
  uint8_t item = 0;
  uint32_t revision = 0;
};

class Navigation {
 public:
  Navigation();
  NavigationSnapshot snapshot() const;
  bool handle(Input input);

 private:
  NavigationSnapshot state_{};
  uint8_t launcher_item_ = 0; // Selection lasts for this boot only.
};
// Translate directional input only while the 2048 feature page is active.
// Keeping this decision in the product domain makes touch input routing
// testable without LVGL, GPIO, or the audio runtime.
bool game_move_for_input(const NavigationSnapshot &navigation, Input input,
                         int &dx, int &dy);

enum class ReminderRepeat : uint8_t { Once, Weekly };
enum class ReminderState : uint8_t { Scheduled, Pending, Missed, Handled };

struct Reminder {
  uint32_t id = 0;
  int64_t due_epoch_seconds = 0;
  ReminderRepeat repeat = ReminderRepeat::Once;
  ReminderState state = ReminderState::Scheduled;
  std::string title;
};

struct PomodoroSnapshot {
  bool active = false;
  bool paused = true;
  uint8_t round = 1;
  uint32_t remaining_seconds = 25 * 60;
  uint32_t revision = 0;
};

struct DueReminder {
  uint32_t id = 0;
  bool missed = false;
};

struct CalendarDay {
  uint8_t day = 0;
  bool in_month = false;
  bool today = false;
  bool has_reminder = false;
};
struct MonthlyCalendar {
  int year = 0;
  uint8_t month = 0;
  uint8_t today = 0;
  std::array<CalendarDay, 42> days{};
};
bool make_monthly_calendar(int year, uint8_t month, uint8_t today,
                           const std::vector<uint8_t> &reminder_days,
                           MonthlyCalendar &result);
// Derive the current local month from trusted wall time and the authoritative
// reminder plans; weekly plans mark every future occurrence in this month.
bool make_local_calendar(int64_t epoch, bool trusted,
                          const std::vector<Reminder> &reminders,
                          const std::vector<DueReminder> &pending,
                          MonthlyCalendar &result);

class TimeState {
 public:
  static constexpr size_t kMaxReminders = 32;
  static constexpr uint32_t kWorkSeconds = 25 * 60;

  bool start_pomodoro(uint32_t monotonic_seconds);
  bool pause_pomodoro(uint32_t monotonic_seconds);
  bool resume_pomodoro(uint32_t monotonic_seconds);
  bool finish_pomodoro();
  PomodoroSnapshot pomodoro(uint32_t monotonic_seconds) const;

  uint32_t add_reminder(int64_t due_epoch_seconds, ReminderRepeat repeat,
                        std::string title);
  bool update_reminder(uint32_t id, int64_t due_epoch_seconds,
                       ReminderRepeat repeat, std::string title);
  bool remove_reminder(uint32_t id);
  bool handle_reminder(uint32_t id);
  std::vector<Reminder> reminders() const;
  std::string_view reminder_title(uint32_t id) const;
  // Restored timers are always paused: monotonic time does not survive reboot.
  bool restore(bool active, uint32_t remaining_seconds, uint8_t round,
               const std::vector<Reminder> &reminders);
  // A device without trusted wall time must not ring.  During a conversation
  // a due reminder becomes Pending and is returned after the conversation.
  std::vector<DueReminder> tick(uint32_t monotonic_seconds,
                                int64_t epoch_seconds, bool time_trusted,
                                bool conversation_active);

 private:
  struct Pomodoro {
    bool active = false;
    bool paused = true;
    uint8_t round = 1;
    uint32_t remaining_seconds = kWorkSeconds;
    uint32_t last_tick = 0;
    uint32_t revision = 0;
  } pomodoro_;
  std::array<Reminder, kMaxReminders> reminders_{};
  size_t reminder_count_ = 0;
  uint32_t next_id_ = 1;
  bool reconcile_after_restore_ = false;
};

struct WoodFish {
  uint32_t count = 0;
  uint32_t last_hit_ms = 0;
  void hit(uint32_t now_ms) { ++count; last_hit_ms = now_ms; }
};

class Game2048 {
 public:
  Game2048();
  explicit Game2048(const std::array<uint16_t, 16> &board);
  const std::array<uint16_t, 16> &board() const { return board_; }
  uint32_t score() const { return score_; }
  bool move(int dx, int dy);
  void reset();

 private:
  std::array<uint16_t, 16> board_{};
  uint32_t score_ = 0;
  uint32_t rng_ = 0x9e3779b9U;
  void spawn();
};

struct ProductSnapshot {
  NavigationSnapshot navigation;
  PomodoroSnapshot pomodoro;
  std::vector<Reminder> reminders;
  WoodFish wood_fish;
  std::array<uint16_t, 16> game_board{};
  uint32_t game_score = 0;
  std::vector<DueReminder> pending_reminders;
  bool time_storage_ready = true;
  std::string time_storage_error;
  bool time_operation_pending = false;
};

class ProductState {
 public:
  ProductSnapshot snapshot(uint32_t monotonic_seconds, uint32_t now_ms) const;
  size_t tick_time(uint32_t monotonic_seconds, int64_t epoch_seconds,
                   bool time_trusted, bool conversation_active);
  bool restore_time(bool active, uint32_t remaining_seconds, uint8_t round,
                    const std::vector<Reminder> &reminders,
                    const std::vector<DueReminder> &pending);
  Navigation navigation;
  TimeState time;
  WoodFish wood_fish;
  Game2048 game;
  std::vector<DueReminder> pending_reminders;
};

const char *page_name(Page page);
const char *module_name(Module module);

}  // namespace smart_speaker::product
