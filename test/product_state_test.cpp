#include "product_state.h"

#include <cassert>
#include <cstdint>
#include <ctime>
#include <string>

using namespace smart_speaker::product;

int main() {
  MonthlyCalendar calendar;
  assert(make_monthly_calendar(2024, 2, 29, {1, 14, 29}, calendar));
  assert(calendar.days[3].day == 1 && calendar.days[3].in_month);
  assert(calendar.days[3].has_reminder && !calendar.days[3].today);
  assert(calendar.days[16].day == 14 && calendar.days[16].has_reminder);
  assert(calendar.days[31].day == 29 && calendar.days[31].today && calendar.days[31].has_reminder);
  assert(calendar.days[32].day == 1 && !calendar.days[32].in_month);
  assert(make_monthly_calendar(2023, 1, 31, {}, calendar));
  assert(calendar.days[0].day == 26 && !calendar.days[0].in_month);
  assert(!make_monthly_calendar(2023, 2, 29, {}, calendar));
  assert(!make_monthly_calendar(2024, 13, 1, {}, calendar));
  assert(!make_monthly_calendar(2024, 2, 1, {30}, calendar));
  // Use mktime so host timezone and device timezone use identical semantics.
  std::tm local = {}; local.tm_year = 124; local.tm_mon = 1; local.tm_mday = 1; local.tm_hour = 12; local.tm_isdst = -1;
  const int64_t february = std::mktime(&local);
  std::vector<Reminder> calendar_reminders{{1, february, ReminderRepeat::Weekly, ReminderState::Scheduled, "weekly"},
      {2, february + 86400, ReminderRepeat::Once, ReminderState::Handled, "done"}};
  assert(!make_local_calendar(february, false, calendar_reminders, {}, calendar));
  assert(make_local_calendar(february, true, calendar_reminders, {}, calendar));
  unsigned marked = 0;
  for (const auto &day : calendar.days) if (day.has_reminder) ++marked;
  assert(marked == 5); // Feb 1, 8, 15, 22 and leap day.
  assert(make_local_calendar(february, true, calendar_reminders, {{2, false}}, calendar));
  marked = 0; for (const auto &day : calendar.days) if (day.has_reminder) ++marked;
  assert(marked == 6); // Unviewed one-time reminder remains visible.
  Navigation navigation;
  assert(navigation.snapshot().page == Page::Desktop);
  assert(navigation.handle(Input::Confirm));
  assert(navigation.snapshot().page == Page::ModuleList);
  assert(navigation.handle(Input::Down));
  assert(navigation.snapshot().item == 1);
  assert(navigation.handle(Input::Confirm));
  assert(navigation.snapshot().module == Module::Assistant);
  assert(navigation.snapshot().page == Page::ModuleMenu);
  assert(navigation.handle(Input::Confirm));
  assert(navigation.snapshot().page == Page::Feature);
  assert(navigation.handle(Input::Back));
  assert(navigation.handle(Input::Back));
  assert(navigation.snapshot().page == Page::ModuleList);
  assert(navigation.handle(Input::Home));

  assert(navigation.handle(Input::Confirm));
  assert(navigation.snapshot().item == 1); // Same boot remembers Assistant.
  assert(navigation.handle(Input::Up));
  const auto edge_revision = navigation.snapshot().revision;
  assert(!navigation.handle(Input::Up));
  assert(navigation.snapshot().revision == edge_revision);
  for (int i = 0; i < 20; ++i) navigation.handle(Input::Down);
  assert(navigation.snapshot().item == 6);
  assert(!navigation.handle(Input::Down));
  assert(navigation.handle(Input::Confirm));
  assert(navigation.snapshot().page == Page::Settings);
  assert(navigation.handle(Input::Back));
  assert(navigation.snapshot().page == Page::ModuleList && navigation.snapshot().item == 6);
  assert(navigation.handle(Input::Home));
  assert(navigation.handle(Input::Confirm) && navigation.snapshot().item == 6);
  Navigation reboot;
  assert(reboot.handle(Input::Confirm) && reboot.snapshot().item == 0);

  Navigation music_navigation;
  assert(music_navigation.handle(Input::Confirm));
  assert(music_navigation.snapshot().page == Page::ModuleList);
  assert(music_navigation.snapshot().module == Module::Music);
  assert(music_navigation.handle(Input::Confirm));
  assert(music_navigation.snapshot().page == Page::ModuleMenu);
  assert(music_navigation.handle(Input::Down));
  assert(music_navigation.snapshot().item == 1);
  assert(music_navigation.handle(Input::Confirm));
  assert(music_navigation.snapshot().page == Page::Feature);
  assert(music_navigation.snapshot().item == 1);
  NavigationSnapshot game_navigation;
  game_navigation.page = Page::Feature;
  game_navigation.module = Module::Games;
  game_navigation.item = 1;
  int dx = 99, dy = 99;
  assert(game_move_for_input(game_navigation, Input::Up, dx, dy) && dx == 0 && dy == -1);
  assert(game_move_for_input(game_navigation, Input::Down, dx, dy) && dx == 0 && dy == 1);
  assert(game_move_for_input(game_navigation, Input::Left, dx, dy) && dx == -1 && dy == 0);
  assert(game_move_for_input(game_navigation, Input::Right, dx, dy) && dx == 1 && dy == 0);
  assert(!game_move_for_input(game_navigation, Input::Back, dx, dy));
  game_navigation.item = 0;
  assert(!game_move_for_input(game_navigation, Input::Up, dx, dy));
  game_navigation.item = 1;
  game_navigation.module = Module::Music;
  assert(!game_move_for_input(game_navigation, Input::Right, dx, dy));
  game_navigation.module = Module::Games;
  game_navigation.page = Page::ModuleMenu;
  assert(!game_move_for_input(game_navigation, Input::Down, dx, dy));

  TimeState time;
  assert(time.start_pomodoro(10));
  assert(time.pomodoro(70).remaining_seconds == 25 * 60 - 60);
  assert(time.pause_pomodoro(70));
  assert(time.pomodoro(500).paused);
  assert(time.resume_pomodoro(500));
  assert(time.finish_pomodoro());
  // Restoring a running timer must not apply wall time elapsed while powered
  // off, nor auto-resume the microphone/audio/UI services.
  TimeState restored;
  assert(restored.restore(true, TimeState::kWorkSeconds, 1, {}));
  assert(restored.pomodoro(9999).active);
  assert(restored.pomodoro(9999).paused);
  assert(restored.pomodoro(9999).remaining_seconds == TimeState::kWorkSeconds);
  assert(restored.resume_pomodoro(9999));
  assert(restored.pomodoro(10059).remaining_seconds == TimeState::kWorkSeconds - 60);
  assert(restored.restore(true, 420, 3, {}));
  assert(restored.pomodoro(1).paused && restored.pomodoro(1).remaining_seconds == 420);
  assert(!restored.restore(true, 99999, 1, {}));
  assert(restored.pomodoro(1).remaining_seconds == 420);
  const auto reminder = time.add_reminder(1000, ReminderRepeat::Once, "test");
  assert(reminder != 0);
  assert(time.tick(1, 999, false, false).empty());
  assert(time.tick(1, 1000, true, true).size() == 1);
  assert(time.reminders()[0].state == ReminderState::Pending);
  assert(time.tick(2, 1000, true, false).size() == 1);
  assert(time.reminders()[0].state == ReminderState::Handled);
  const auto weekly = time.add_reminder(2000, ReminderRepeat::Weekly, "weekly");
  assert(time.tick(3, 2000 + 2 * 24 * 60 * 60, true, false).size() == 1);
  assert(time.reminders()[1].state == ReminderState::Scheduled);
  assert(time.reminders()[1].due_epoch_seconds == 2000 + 7 * 24 * 60 * 60);
  assert(time.handle_reminder(weekly));
  assert(time.reminders()[1].state == ReminderState::Scheduled);
  assert(time.tick(4, 2000 + 7 * 24 * 60 * 60, true, false).size() == 1);
  assert(time.reminders()[1].due_epoch_seconds == 2000 + 14 * 24 * 60 * 60);

  TimeState deferred_time;
  const auto deferred_weekly = deferred_time.add_reminder(30'000, ReminderRepeat::Weekly, "later weekly");
  const int64_t weekly_due = 30'000;
  assert(deferred_time.tick(5, weekly_due, true, true).size() == 1);
  assert(deferred_time.reminders()[0].state == ReminderState::Pending);
  assert(deferred_time.reminders()[0].due_epoch_seconds == 30'000);
  assert(deferred_time.tick(6, weekly_due, true, false).size() == 1);
  assert(deferred_time.reminders()[0].state == ReminderState::Scheduled);
  assert(deferred_time.reminders()[0].due_epoch_seconds == 30'000 + 7 * 24 * 60 * 60);
  assert(deferred_time.tick(7, weekly_due, true, false).empty());
  assert(deferred_time.handle_reminder(deferred_weekly));
  assert(deferred_time.reminders()[0].state == ReminderState::Scheduled);
  const int64_t jumped = 30'000 + 100 * 7 * 24 * 60 * 60;
  assert(deferred_time.tick(8, jumped, true, false).size() == 1);
  assert(deferred_time.tick(9, jumped, true, false).empty());
  assert(deferred_time.reminders()[0].due_epoch_seconds == jumped + 7 * 24 * 60 * 60);

  ProductState product_state;
  const auto deferred = product_state.time.add_reminder(5000, ReminderRepeat::Once, "later");
  assert(product_state.tick_time(10, 4999, false, true) == 0);
  assert(product_state.tick_time(10, 5000, true, true) == 1);
  assert(product_state.snapshot(10, 0).pending_reminders.size() == 1);
  assert(product_state.tick_time(11, 5000, true, false) == 1);
  assert(product_state.snapshot(11, 0).pending_reminders.size() == 1);
  assert(product_state.time.handle_reminder(deferred));

  ComputerPairing pairing;
  ComputerTelemetry sample;
  sample.cpu = TelemetryStatus::Fresh;
  sample.cpu_percent = 42;
  assert(pairing.pair("macbook"));
  pairing.update("other", sample, 10);
  assert(!pairing.update("other", sample, 10));
  assert(pairing.snapshot(10).cpu == TelemetryStatus::Offline);
  assert(pairing.update("macbook", sample, 10));
  assert(pairing.snapshot(20).cpu == TelemetryStatus::Fresh);
  assert(pairing.snapshot(10020).cpu == TelemetryStatus::Stale);
  assert(!pairing.disconnect("other"));
  assert(pairing.disconnect("macbook"));
  assert(pairing.snapshot(20).cpu == TelemetryStatus::Offline);

  Game2048 game;
  Game2048 merge(std::array<uint16_t, 16>{2, 2, 2, 2});
  assert(merge.move(-1, 0));
  assert(merge.board()[0] == 4 && merge.board()[1] == 4);
  assert(merge.score() == 8);
  Game2048 chain(std::array<uint16_t, 16>{2, 2, 4, 0});
  assert(chain.move(-1, 0));
  assert(chain.board()[0] == 4 && chain.board()[1] == 4);
  assert(chain.score() == 4);
  const auto before = game.board();
  assert(game.move(1, 0) || game.move(-1, 0) || game.move(0, 1) || game.move(0, -1));
  assert(game.board() != before);
  uint32_t tile_total = 0;
  for (auto tile : game.board()) tile_total += tile;
  assert(tile_total >= 4);
  for (int i = 0; i < 1000; ++i) {
    const int direction = i % 4;
    game.move(direction == 0 ? 1 : direction == 1 ? -1 : 0,
              direction == 2 ? 1 : direction == 3 ? -1 : 0);
    uint32_t total = 0;
    for (auto tile : game.board()) total += tile;
    assert(total >= tile_total);
    tile_total = total;
  }
  assert(!game.move(2, 0));
  game.reset();
  assert(game.score() == 0);
}
