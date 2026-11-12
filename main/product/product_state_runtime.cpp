#include "product_state_runtime.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>

#include <ArduinoJson.h>
#include "lunaredu_lunaros/persistence/storage.h"

namespace smart_speaker::product {
namespace storage = lunaredu::lunaros;
namespace {
ProductState g_state;
std::mutex g_mutex;
// Only time writers take this lock. Display/navigation readers never
// wait on flash I/O while holding the product snapshot lock.
std::mutex g_time_io_mutex;
constexpr const char *kTimePath = "/user/product_time.json";
constexpr size_t kMaxPersistedBytes = 16 * 1024;
bool g_time_loaded = false;
std::string g_time_error = "time_not_loaded";
uint32_t g_time_now = 0;
uint32_t g_checkpoint_at = 0;
uint8_t g_checkpoint_failures = 0;
uint32_t g_retry_after = 0;
std::atomic<PomodoroAction> g_pomodoro_request{PomodoroAction::None};

void set_time_error(std::string error) {
  std::lock_guard lock(g_mutex);
  g_time_error = std::move(error);
}

bool persist_time(const ProductState &candidate, uint32_t now) {
  const auto state = candidate.snapshot(now, 0);
  JsonDocument document;
  document["schema"] = 1;
  document["active"] = state.pomodoro.active;
  document["remaining_seconds"] = state.pomodoro.remaining_seconds;
  document["round"] = state.pomodoro.round;
  JsonArray reminders = document["reminders"].to<JsonArray>();
  for (const auto &reminder : state.reminders) {
    JsonObject entry = reminders.add<JsonObject>();
    entry["id"] = reminder.id;
    entry["due"] = reminder.due_epoch_seconds;
    entry["repeat"] = static_cast<uint8_t>(reminder.repeat);
    entry["state"] = static_cast<uint8_t>(reminder.state);
    entry["title"] = reminder.title;
  }
  JsonArray pending = document["pending"].to<JsonArray>();
  for (const auto &event : state.pending_reminders) {
    JsonObject entry = pending.add<JsonObject>();
    entry["id"] = event.id;
    entry["missed"] = event.missed;
  }
  if (document.overflowed() || measureJson(document) > kMaxPersistedBytes) {
    set_time_error("time_record_too_large");
    return false;
  }
  std::string serialized;
  serializeJson(document, serialized);
  if (!storage::storage_write_text_file(kTimePath, serialized.c_str())) {
    set_time_error("time_write_failed");
    return false;
  }
  return true;
}

bool read_time(ProductState &candidate) {
  if (!storage::storage_ready()) { set_time_error("time_storage_unavailable"); return false; }
  if (!storage::storage_path_exists(kTimePath)) return true;
  std::string serialized;
  const bool read = storage::storage_stream_file(kTimePath,
      [](const uint8_t *data, size_t size, void *context) {
        auto &body = *static_cast<std::string *>(context);
        if (size > kMaxPersistedBytes - body.size()) return false;
        body.append(reinterpret_cast<const char *>(data), size);
        return true;
      }, &serialized);
  if (!read) { set_time_error("time_read_failed"); return false; }
  JsonDocument document;
  if (deserializeJson(document, serialized, DeserializationOption::NestingLimit(6)) ||
      document["schema"] != 1 || !document["active"].is<bool>() ||
      !document["remaining_seconds"].is<uint32_t>() || !document["round"].is<uint8_t>() ||
      !document["reminders"].is<JsonArray>() || !document["pending"].is<JsonArray>() ||
      document["reminders"].size() > TimeState::kMaxReminders ||
      document["pending"].size() > TimeState::kMaxReminders) return false;
  std::vector<Reminder> reminders;
  for (JsonVariantConst value : document["reminders"].as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>()) return false;
    const auto entry = value.as<JsonObjectConst>();
    if (!entry["id"].is<uint32_t>() || !entry["due"].is<int64_t>() ||
        !entry["repeat"].is<uint8_t>() || !entry["state"].is<uint8_t>() ||
        !entry["title"].is<const char *>()) return false;
    const uint8_t repeat = entry["repeat"].as<uint8_t>();
    const uint8_t state = entry["state"].as<uint8_t>();
    if (repeat > static_cast<uint8_t>(ReminderRepeat::Weekly) ||
        state > static_cast<uint8_t>(ReminderState::Handled)) return false;
    reminders.push_back({entry["id"].as<uint32_t>(), entry["due"].as<int64_t>(),
        static_cast<ReminderRepeat>(repeat), static_cast<ReminderState>(state),
        entry["title"].as<std::string>()});
  }
  std::vector<DueReminder> pending;
  for (JsonVariantConst value : document["pending"].as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>()) return false;
    const auto entry = value.as<JsonObjectConst>();
    if (!entry["id"].is<uint32_t>() || !entry["missed"].is<bool>()) return false;
    pending.push_back({entry["id"].as<uint32_t>(), entry["missed"].as<bool>()});
  }
  return candidate.restore_time(document["active"].as<bool>(),
      document["remaining_seconds"].as<uint32_t>(), document["round"].as<uint8_t>(),
      reminders, pending);
}

ProductState time_candidate() {
  std::lock_guard lock(g_mutex);
  return g_state;
}

void publish_time(ProductState candidate, uint32_t now) {
  std::lock_guard lock(g_mutex);
  g_state.time = std::move(candidate.time);
  g_state.pending_reminders = std::move(candidate.pending_reminders);
  g_time_error.clear();
  g_checkpoint_at = now;
  g_checkpoint_failures = 0;
  g_retry_after = 0;
}

// This narrow transaction is specific to the durable time record. Readers
// continue seeing the old coherent state until the atomic file publish succeeds.
template <typename Operation>
auto change_time(uint32_t now, Operation operation) {
  std::lock_guard io(g_time_io_mutex);
  using Result = decltype(operation(std::declval<ProductState &>()));
  if (!g_time_loaded) return Result{};
  if (now != 0) g_time_now = now;
  auto candidate = time_candidate();
  const auto result = operation(candidate);
  if (!result || !persist_time(candidate, g_time_now)) return Result{};
  publish_time(std::move(candidate), g_time_now);
  return result;
}
} // namespace

ProductSnapshot product_state_snapshot(uint32_t seconds, uint32_t now_ms) {
  std::lock_guard lock(g_mutex);
  auto state = g_state.snapshot(seconds, now_ms);
  state.time_storage_ready = g_time_loaded;
  state.time_storage_error = g_time_error;
  state.time_operation_pending = g_pomodoro_request.load() != PomodoroAction::None;
  return state;
}

NavigationSnapshot product_navigation_snapshot() {
  std::lock_guard lock(g_mutex);
  return g_state.navigation.snapshot();
}

std::string product_pending_reminder_hint() {
  std::lock_guard lock(g_mutex);
  if (g_state.pending_reminders.empty()) return {};
  const auto &event = g_state.pending_reminders.back();
  const auto title = g_state.time.reminder_title(event.id);
  if (title.empty()) return {};
  std::string hint = event.missed ? "错过提醒：" : "提醒：";
  hint.append(title);
  return hint;
}
bool product_time_load(uint32_t monotonic_seconds) {
  std::lock_guard io(g_time_io_mutex);
  ProductState candidate;
  if (!read_time(candidate)) {
    std::lock_guard lock(g_mutex);
    g_time_loaded = false;
    if (g_time_error.empty() || g_time_error == "time_not_loaded") g_time_error = "time_record_invalid";
    return false;
  }
  publish_time(std::move(candidate), 0);
  {
    std::lock_guard lock(g_mutex);
    g_time_loaded = true;
    g_time_now = monotonic_seconds;
    g_checkpoint_at = monotonic_seconds;
  }
  return true;
}

bool product_time_save(uint32_t seconds, uint32_t) {
  return change_time(seconds, [](ProductState &) { return true; });
}

size_t product_tick_time(uint32_t seconds, int64_t epoch, bool trusted, bool conversation) {
  std::lock_guard io(g_time_io_mutex);
  if (!g_time_loaded) return 0;
  if (g_checkpoint_failures >= 3 || seconds < g_retry_after) return 0;
  g_time_now = seconds;
  auto candidate = time_candidate();
  const auto due = candidate.tick_time(seconds, epoch, trusted, conversation);
  const auto timer = candidate.time.pomodoro(seconds);
  if (!due && !(timer.active && !timer.paused && seconds - g_checkpoint_at >= 30)) {
    std::lock_guard lock(g_mutex);
    g_state.time = std::move(candidate.time);
    return 0;
  }
  if (!persist_time(candidate, seconds)) {
    ++g_checkpoint_failures;
    g_retry_after = seconds + (g_checkpoint_failures == 1 ? 5U : 15U);
    return 0;
  }
  publish_time(std::move(candidate), seconds);
  return due;
}

bool product_navigation_input(Input input) { std::lock_guard lock(g_mutex); return g_state.navigation.handle(input); }
bool product_pomodoro_start(uint32_t s) { return change_time(s, [s](ProductState &c) { return c.time.start_pomodoro(s); }); }
bool product_pomodoro_pause(uint32_t s) { return change_time(s, [s](ProductState &c) { return c.time.pause_pomodoro(s); }); }
bool product_pomodoro_resume(uint32_t s) { return change_time(s, [s](ProductState &c) { return c.time.resume_pomodoro(s); }); }
bool product_pomodoro_finish() { return change_time(0, [](ProductState &c) { return c.time.finish_pomodoro(); }); }
bool product_request_pomodoro(PomodoroAction action) {
  if (action == PomodoroAction::None || action > PomodoroAction::Finish) return false;
  auto empty = PomodoroAction::None;
  return g_pomodoro_request.compare_exchange_strong(empty, action);
}
void product_poll_pomodoro_request(uint32_t seconds) {
  const auto action = g_pomodoro_request.load();
  if (action == PomodoroAction::None) return;
  const bool applied = action == PomodoroAction::Start ? product_pomodoro_start(seconds) :
      action == PomodoroAction::Pause ? product_pomodoro_pause(seconds) :
      action == PomodoroAction::Resume ? product_pomodoro_resume(seconds) : product_pomodoro_finish();
  if (!applied) {
    std::lock_guard lock(g_mutex);
    if (g_time_error.empty()) g_time_error = "pomodoro_action_not_applied";
  }
  g_pomodoro_request.store(PomodoroAction::None);
}
uint32_t product_add_reminder(int64_t due, ReminderRepeat repeat, std::string title) {
  return change_time(0, [&](ProductState &c) { return c.time.add_reminder(due, repeat, std::move(title)); });
}
bool product_update_reminder(uint32_t id, int64_t due, ReminderRepeat repeat, std::string title) {
  return change_time(0, [&](ProductState &c) {
    if (!c.time.update_reminder(id, due, repeat, std::move(title))) return false;
    c.pending_reminders.erase(std::remove_if(c.pending_reminders.begin(), c.pending_reminders.end(),
        [id](const DueReminder event) { return event.id == id; }), c.pending_reminders.end());
    return true;
  });
}
bool product_remove_reminder(uint32_t id) {
  return change_time(0, [id](ProductState &c) {
    if (!c.time.remove_reminder(id)) return false;
    c.pending_reminders.erase(std::remove_if(c.pending_reminders.begin(), c.pending_reminders.end(),
        [id](const DueReminder event) { return event.id == id; }), c.pending_reminders.end());
    return true;
  });
}
bool product_handle_reminder(uint32_t id) {
  return change_time(0, [id](ProductState &c) {
    if (!c.time.handle_reminder(id)) return false;
    c.pending_reminders.erase(std::remove_if(c.pending_reminders.begin(), c.pending_reminders.end(),
        [id](const DueReminder event) { return event.id == id; }), c.pending_reminders.end());
    return true;
  });
}
WoodFish product_wood_fish_snapshot() { std::lock_guard lock(g_mutex); return g_state.wood_fish; }
void product_hit_wood_fish(uint32_t now) { std::lock_guard lock(g_mutex); g_state.wood_fish.hit(now); }
bool product_move_2048(int dx, int dy) { std::lock_guard lock(g_mutex); return g_state.game.move(dx, dy); }
void product_reset_2048() { std::lock_guard lock(g_mutex); g_state.game.reset(); }
} // namespace smart_speaker::product
