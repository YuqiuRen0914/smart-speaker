#include "product_state.h"
#include "pet_animation_pack.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace smart_speaker::product {
namespace {
uint8_t module_count() { return 6; }
uint8_t child_count(Module module) {
  switch (module) {
    case Module::Music: return 2;       // local library, white noise
    case Module::Assistant: return 2;   // voice and pet action catalog
    case Module::Time: return 2;       // clock, pomodoro/reminders
    case Module::Games: return 2;      // wood fish, 2048
    case Module::Information: return 2; // calendar, weather
    case Module::Settings: return 3;  // network, device info, account
    default: return 1;
  }
}

int64_t next_week(int64_t due, int64_t now) {
  constexpr int64_t week = 7 * 24 * 60 * 60;
  return due + ((now - due) / week + 1) * week;
}
bool leap_year(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
uint8_t month_days(int year, uint8_t month) {
  static constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month == 2 && leap_year(year) ? 29 : days[month - 1];
}
// Days since 1970-01-01, with 0=Monday weekday mapping at the call site.
int64_t civil_days(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned shifted_month = month > 2 ? month - 3 : month + 9;
  const unsigned doy = (153 * shifted_month + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return int64_t(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}
}

bool make_monthly_calendar(int year, uint8_t month, uint8_t today,
                           const std::vector<uint8_t> &reminder_days,
                           MonthlyCalendar &result) {
  if (year < 1970 || year > 2099 || month < 1 || month > 12 ||
      today > month_days(year, month)) return false;
  for (const uint8_t day : reminder_days) if (day < 1 || day > month_days(year, month)) return false;
  MonthlyCalendar candidate;
  candidate.year = year; candidate.month = month; candidate.today = today;
  const int first = static_cast<int>((civil_days(year, month, 1) + 3) % 7); // Monday first.
  const uint8_t count = month_days(year, month);
  const int previous_year = month == 1 ? year - 1 : year;
  const uint8_t previous_month = month == 1 ? 12 : month - 1;
  const uint8_t previous_count = month_days(previous_year, previous_month);
  for (int index = 0; index < 42; ++index) {
    const int calendar_day = index - first + 1;
    auto &cell = candidate.days[static_cast<size_t>(index)];
    if (calendar_day < 1) cell.day = static_cast<uint8_t>(previous_count + calendar_day);
    else if (calendar_day > count) cell.day = static_cast<uint8_t>(calendar_day - count);
    else {
      cell.day = static_cast<uint8_t>(calendar_day);
      cell.in_month = true;
      cell.today = today != 0 && cell.day == today;
      cell.has_reminder = std::find(reminder_days.begin(), reminder_days.end(), cell.day) != reminder_days.end();
    }
  }
  result = candidate;
  return true;
}

bool make_local_calendar(int64_t epoch, bool trusted,
                          const std::vector<Reminder> &reminders,
                          const std::vector<DueReminder> &pending,
                          MonthlyCalendar &result) {
  if (!trusted || epoch < 0 || epoch > 4'102'444'800LL) return false;
  auto local_time = [](int64_t value, std::tm &out) {
    const std::time_t when = static_cast<std::time_t>(value);
#ifdef _WIN32
    return localtime_s(&out, &when) == 0;
#else
    return localtime_r(&when, &out) != nullptr;
#endif
  };
  std::tm today = {};
  if (!local_time(epoch, today)) return false;
  std::vector<uint8_t> days;
  for (const auto &reminder : reminders) {
    const bool has_pending = std::any_of(pending.begin(), pending.end(),
        [&](const DueReminder &event) { return event.id == reminder.id; });
    if (reminder.state == ReminderState::Handled && !has_pending) continue;
    int64_t due = reminder.due_epoch_seconds;
    std::tm date = {};
    // At most five weekly occurrences fit in a month. An additional initial
    // occurrence can precede this month after a device has been offline.
    if (reminder.repeat == ReminderRepeat::Weekly && due < epoch - 31 * 86400LL)
      due += ((epoch - 31 * 86400LL - due) / (7 * 86400LL)) * (7 * 86400LL);
    for (int count = 0; count < 10; ++count) {
      if (!local_time(due, date)) break;
      if (date.tm_year == today.tm_year && date.tm_mon == today.tm_mon)
        days.push_back(static_cast<uint8_t>(date.tm_mday));
      if (reminder.repeat != ReminderRepeat::Weekly ||
          date.tm_year > today.tm_year || (date.tm_year == today.tm_year && date.tm_mon > today.tm_mon)) break;
      due += 7 * 86400LL;
    }
  }
  return make_monthly_calendar(today.tm_year + 1900, static_cast<uint8_t>(today.tm_mon + 1),
      static_cast<uint8_t>(today.tm_mday), days, result);
}

Navigation::Navigation() = default;

NavigationSnapshot Navigation::snapshot() const { return state_; }

bool Navigation::handle(Input input) {
  const auto old = state_;
  if (input == Input::Home) {
    state_.page = Page::Desktop;
    state_.item = 0;
  } else if (input == Input::Assistant) {
    state_.page = Page::Feature;
    state_.module = Module::Assistant;
    state_.item = 0;
  } else {
    switch (state_.page) {
      case Page::Desktop:
        if (input == Input::Confirm) {
          state_.page = Page::ModuleList;
          state_.item = launcher_item_;
        }
        break;
      case Page::ModuleList:
        if (input == Input::Up && state_.item > 0) --state_.item;
        else if (input == Input::Down && state_.item + 1 < module_count()) ++state_.item;
        else if (input == Input::Confirm) {
          state_.module = static_cast<Module>(state_.item);
          state_.item = 0;
          state_.page = state_.module == Module::Settings ? Page::Settings : Page::ModuleMenu;
        } else if (input == Input::Back) state_.page = Page::Desktop;
        break;
      case Page::ModuleMenu:
        if (input == Input::Up && state_.item > 0) --state_.item;
        else if (input == Input::Down && state_.item + 1 < child_count(state_.module)) ++state_.item;
        else if (input == Input::Confirm) {
          if (state_.module == Module::Assistant && state_.item == 1) {
            state_.page = Page::PetActions;
            state_.item = 0;
          } else state_.page = Page::Feature;
        }
        else if (input == Input::Back) { state_.page = Page::ModuleList; state_.item = static_cast<uint8_t>(state_.module); }
        break;
      case Page::Feature:
        if (input == Input::Back) state_.page = Page::ModuleMenu;
        break;
      case Page::Settings:
        if (input == Input::Up && state_.item > 0) --state_.item;
        else if (input == Input::Down && state_.item + 1 < child_count(Module::Settings)) ++state_.item;
        else if (input == Input::Back) { state_.page = Page::ModuleList; state_.item = static_cast<uint8_t>(Module::Settings); }
        break;
      case Page::PetActions:
        if (input == Input::Up) state_.item = (state_.item + kPetActionRows - 1) % kPetActionRows;
        else if (input == Input::Down) state_.item = (state_.item + 1) % kPetActionRows;
        else if (input == Input::Back) { state_.page = Page::ModuleMenu; state_.item = 1; }
        break;
    }
  }
  if (state_.page == Page::ModuleList) launcher_item_ = state_.item;
  if (old.page != state_.page || old.item != state_.item || old.module != state_.module) ++state_.revision;
  return old.page != state_.page || old.item != state_.item || old.module != state_.module;
}

bool game_move_for_input(const NavigationSnapshot &navigation, Input input,
                         int &dx, int &dy) {
  if (navigation.page != Page::Feature || navigation.module != Module::Games ||
      navigation.item != 1) return false;
  switch (input) {
    case Input::Up: dx = 0; dy = -1; return true;
    case Input::Down: dx = 0; dy = 1; return true;
    case Input::Left: dx = -1; dy = 0; return true;
    case Input::Right: dx = 1; dy = 0; return true;
    default: return false;
  }
}

bool TimeState::start_pomodoro(uint32_t monotonic_seconds) {
  if (pomodoro_.active) return false;
  pomodoro_.round = 1;
  pomodoro_.remaining_seconds = kWorkSeconds;
  pomodoro_.active = true; pomodoro_.paused = false; pomodoro_.last_tick = monotonic_seconds; ++pomodoro_.revision; return true;
}
bool TimeState::pause_pomodoro(uint32_t monotonic_seconds) {
  if (!pomodoro_.active || pomodoro_.paused) return false;
  const uint32_t elapsed = monotonic_seconds >= pomodoro_.last_tick ? monotonic_seconds - pomodoro_.last_tick : 0;
  pomodoro_.remaining_seconds = elapsed >= pomodoro_.remaining_seconds ? 0 : pomodoro_.remaining_seconds - elapsed;
  pomodoro_.paused = true; ++pomodoro_.revision; return true;
}
bool TimeState::resume_pomodoro(uint32_t monotonic_seconds) {
  if (!pomodoro_.active || !pomodoro_.paused || pomodoro_.remaining_seconds == 0) return false;
  pomodoro_.paused = false; pomodoro_.last_tick = monotonic_seconds; ++pomodoro_.revision; return true;
}
bool TimeState::finish_pomodoro() {
  if (!pomodoro_.active) return false;
  pomodoro_.active = false; pomodoro_.paused = true; pomodoro_.remaining_seconds = kWorkSeconds; ++pomodoro_.revision; return true;
}
PomodoroSnapshot TimeState::pomodoro(uint32_t monotonic_seconds) const {
  auto copy = pomodoro_;
  if (copy.active && !copy.paused && monotonic_seconds >= copy.last_tick) {
    const uint32_t elapsed = monotonic_seconds - copy.last_tick;
    copy.remaining_seconds = elapsed >= copy.remaining_seconds ? 0 : copy.remaining_seconds - elapsed;
  }
  return {copy.active, copy.paused, copy.round, copy.remaining_seconds, copy.revision};
}

uint32_t TimeState::add_reminder(int64_t due, ReminderRepeat repeat, std::string title) {
  if (reminder_count_ == reminders_.size() || title.empty() || title.size() > 64 ||
      due <= 0 || due > 4'102'444'800LL ||
      (repeat != ReminderRepeat::Once && repeat != ReminderRepeat::Weekly)) return 0;
  while (next_id_ == 0 || std::any_of(reminders_.begin(), reminders_.begin() + reminder_count_,
      [&](const Reminder &reminder) { return reminder.id == next_id_; })) ++next_id_;
  reminders_[reminder_count_] = {next_id_++, due, repeat, ReminderState::Scheduled, std::move(title)};
  return reminders_[reminder_count_++].id;
}
bool TimeState::update_reminder(uint32_t id, int64_t due, ReminderRepeat repeat, std::string title) {
  if (title.empty() || title.size() > 64 || due <= 0 || due > 4'102'444'800LL ||
      (repeat != ReminderRepeat::Once && repeat != ReminderRepeat::Weekly)) return false;
  for (size_t i = 0; i < reminder_count_; ++i) if (reminders_[i].id == id) { reminders_[i].due_epoch_seconds = due; reminders_[i].repeat = repeat; reminders_[i].title = std::move(title); reminders_[i].state = ReminderState::Scheduled; return true; }
  return false;
}
bool TimeState::remove_reminder(uint32_t id) {
  for (size_t i = 0; i < reminder_count_; ++i) if (reminders_[i].id == id) { reminders_[i] = reminders_[--reminder_count_]; return true; }
  return false;
}
bool TimeState::handle_reminder(uint32_t id) {
  for (size_t i = 0; i < reminder_count_; ++i) {
    auto &reminder = reminders_[i];
    if (reminder.id != id) continue;
    // A weekly reminder is a recurring plan. Handling one occurrence must not
    // disable the next occurrence, which was already scheduled at delivery.
    if (reminder.repeat == ReminderRepeat::Once)
      reminder.state = ReminderState::Handled;
    return true;
  }
  return false;
}
std::vector<Reminder> TimeState::reminders() const { return {reminders_.begin(), reminders_.begin() + reminder_count_}; }
std::string_view TimeState::reminder_title(uint32_t id) const {
  for (size_t i = 0; i < reminder_count_; ++i)
    if (reminders_[i].id == id) return reminders_[i].title;
  return {};
}
bool TimeState::restore(bool active, uint32_t remaining_seconds, uint8_t round,
                        const std::vector<Reminder> &reminders) {
  if (remaining_seconds > kWorkSeconds || round == 0 || round > 4 ||
      reminders.size() > kMaxReminders) return false;
  std::array<Reminder, kMaxReminders> restored{};
  uint32_t highest_id = 0;
  for (size_t i = 0; i < reminders.size(); ++i) {
    const auto &reminder = reminders[i];
    if (reminder.id == 0 || reminder.title.empty() || reminder.title.size() > 64 ||
        reminder.due_epoch_seconds <= 0 || reminder.due_epoch_seconds > 4'102'444'800LL ||
        (reminder.repeat != ReminderRepeat::Once && reminder.repeat != ReminderRepeat::Weekly) ||
        (reminder.state != ReminderState::Scheduled && reminder.state != ReminderState::Pending &&
         reminder.state != ReminderState::Missed && reminder.state != ReminderState::Handled)) return false;
    for (size_t j = 0; j < i; ++j) if (restored[j].id == reminder.id) return false;
    restored[i] = reminder;
    if (restored[i].state == ReminderState::Pending) restored[i].state =
        restored[i].repeat == ReminderRepeat::Weekly ? ReminderState::Scheduled : ReminderState::Missed;
    highest_id = std::max(highest_id, reminder.id);
  }
  reminders_ = std::move(restored);
  reminder_count_ = reminders.size();
  next_id_ = highest_id == UINT32_MAX ? 1 : highest_id + 1;
  pomodoro_ = {};
  pomodoro_.active = active;
  pomodoro_.paused = true;
  pomodoro_.remaining_seconds = remaining_seconds;
  pomodoro_.round = round;
  reconcile_after_restore_ = true;
  ++pomodoro_.revision;
  return true;
}
std::vector<DueReminder> TimeState::tick(uint32_t monotonic_seconds, int64_t epoch, bool trusted, bool conversation) {
  std::vector<DueReminder> due;
  (void)monotonic_seconds;
  if (!trusted) return due;
  const bool reconciling = reconcile_after_restore_;
  reconcile_after_restore_ = false;
  for (size_t i = 0; i < reminder_count_; ++i) {
    auto &r = reminders_[i];
    const bool pending_ready = r.state == ReminderState::Pending && !conversation;
    const bool scheduled_due = r.state == ReminderState::Scheduled && epoch >= r.due_epoch_seconds;
    if (pending_ready || scheduled_due) {
      const bool missed = reconciling || (!pending_ready && epoch - r.due_epoch_seconds > 90);
      if (conversation && !missed) {
        r.state = ReminderState::Pending;
      } else if (r.repeat == ReminderRepeat::Weekly) {
        r.due_epoch_seconds = next_week(r.due_epoch_seconds, epoch);
        r.state = ReminderState::Scheduled;
      } else {
        r.state = missed ? ReminderState::Missed : ReminderState::Handled;
      }
      due.push_back({r.id, missed});
    }
  }
  return due;
}

Game2048::Game2048() { reset(); }
Game2048::Game2048(const std::array<uint16_t, 16> &board) : board_(board) {}
void Game2048::reset() { board_.fill(0); score_ = 0; spawn(); spawn(); }
void Game2048::spawn() {
  std::array<uint8_t, 16> free{}; size_t count = 0;
  for (size_t i = 0; i < board_.size(); ++i) if (!board_[i]) free[count++] = static_cast<uint8_t>(i);
  if (!count) return;
  rng_ = rng_ * 1664525U + 1013904223U;
  board_[free[rng_ % count]] = (rng_ & 7U) == 0 ? 4 : 2;
}
bool Game2048::move(int dx, int dy) {
  if ((dx == 0) == (dy == 0) || std::abs(dx) > 1 || std::abs(dy) > 1) return false;
  auto before = board_;
  for (int line = 0; line < 4; ++line) {
    std::array<uint16_t, 4> values{}; int n = 0;
    for (int pos = 0; pos < 4; ++pos) { int x = dx ? (dx > 0 ? 3 - pos : pos) : line; int y = dy ? (dy > 0 ? 3 - pos : pos) : line; auto v = board_[y * 4 + x]; if (v) values[n++] = v; }
    int out = 0;
    for (int i = 0; i < n;) {
      if (i + 1 < n && values[i] == values[i + 1]) {
        const uint16_t merged = static_cast<uint16_t>(values[i] * 2);
        score_ += merged;
        values[out++] = merged;
        i += 2;
      } else {
        values[out++] = values[i++];
      }
    }
    for (int pos = 0; pos < 4; ++pos) { int x = dx ? (dx > 0 ? 3 - pos : pos) : line; int y = dy ? (dy > 0 ? 3 - pos : pos) : line; board_[y * 4 + x] = pos < out ? values[pos] : 0; }
  }
  if (board_ == before) return false;
  spawn(); return true;
}

ProductSnapshot ProductState::snapshot(uint32_t monotonic_seconds, uint32_t) const {
  ProductSnapshot snapshot;
  snapshot.navigation = navigation.snapshot();
  snapshot.pomodoro = time.pomodoro(monotonic_seconds);
  snapshot.reminders = time.reminders();
  snapshot.wood_fish = wood_fish;
  snapshot.game_board = game.board();
  snapshot.game_score = game.score();
  snapshot.pending_reminders = pending_reminders;
  return snapshot;
}
size_t ProductState::tick_time(uint32_t monotonic_seconds, int64_t epoch_seconds,
                               bool time_trusted, bool conversation_active) {
  const auto due = time.tick(monotonic_seconds, epoch_seconds, time_trusted,
                             conversation_active);
  {
    for (const auto event : due) {
      const auto found = std::find_if(pending_reminders.begin(), pending_reminders.end(),
          [event](const DueReminder &existing) { return existing.id == event.id; });
      if (found == pending_reminders.end() && pending_reminders.size() < TimeState::kMaxReminders)
        pending_reminders.push_back(event);
    }
  }
  return due.size();
}
bool ProductState::restore_time(bool active, uint32_t remaining_seconds, uint8_t round,
                                const std::vector<Reminder> &reminders,
                                const std::vector<DueReminder> &pending) {
  if (pending.size() > TimeState::kMaxReminders) return false;
  for (const auto &event : pending) {
    if (event.id == 0 || std::none_of(reminders.begin(), reminders.end(),
          [event](const Reminder &reminder) { return reminder.id == event.id; })) return false;
  }
  if (!time.restore(active, remaining_seconds, round, reminders)) return false;
  pending_reminders = pending;
  // Reboot never replays an audible notification from an earlier boot.
  for (auto &event : pending_reminders) event.missed = true;
  return true;
}
const char *page_name(Page page) { return page == Page::Desktop ? "desktop" : page == Page::ModuleList ? "module_list" : page == Page::ModuleMenu ? "module_menu" : page == Page::Feature ? "feature" : page == Page::PetActions ? "pet_actions" : "settings"; }
const char *module_name(Module module) { static constexpr const char *names[] = {"music", "assistant", "time", "information", "games", "settings"}; return names[static_cast<uint8_t>(module)]; }
}
