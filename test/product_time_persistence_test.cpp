#include "product/product_state_runtime.h"
#include "product_time_storage_test.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace smart_speaker::product;

int main() {
  test_storage::reset();
  assert(product_time_load(0));
  const uint32_t reminder = product_add_reminder(1'900'000'000, ReminderRepeat::Weekly, "weekly");
  assert(reminder != 0);
  assert(product_pending_reminder_hint().empty());
  assert(test_storage::record.find("\"schema\":1") != std::string::npos);
  assert(test_storage::record.find("weekly") != std::string::npos);

  assert(product_pomodoro_start(100));
  auto state = product_state_snapshot(101, 101'000);
  assert(state.pomodoro.active && !state.pomodoro.paused);
  assert(product_time_save(101, 101'000));
  assert(test_storage::record.find("\"active\":true") != std::string::npos);
  assert(product_time_load(1000));
  state = product_state_snapshot(0, 0);
  assert(state.pomodoro.active && state.pomodoro.paused);
  assert(state.pomodoro.remaining_seconds == 1499);
  assert(state.reminders[0].id == reminder);
  assert(product_pomodoro_resume(1000));
  const auto checkpoint_before = test_storage::writes;
  product_tick_time(1029, 1'800'000'000, true, false);
  assert(test_storage::writes == checkpoint_before);
  product_tick_time(1030, 1'800'000'001, true, false);
  assert(test_storage::writes == checkpoint_before + 1);
  assert(product_time_load(0));
  state = product_state_snapshot(0, 0);
  assert(state.pomodoro.paused && state.pomodoro.remaining_seconds == 1469);

  // Snapshot readers can run during storage. They see the old state until a
  // durable write completes; unrelated navigation must survive publication.
  test_storage::before_write = [] {
    const auto during_write = product_state_snapshot(0, 0);
    assert(during_write.reminders.size() == 1);
    product_navigation_input(Input::Confirm);
  };
  const uint32_t second = product_add_reminder(1'900'000'010, ReminderRepeat::Once, "transaction");
  assert(second);
  test_storage::before_write = {};
  assert(product_state_snapshot(0, 0).navigation.page == Page::ModuleList);
  assert(product_remove_reminder(second));

  test_storage::fail_write = true;
  assert(product_add_reminder(1'900'000'100, ReminderRepeat::Once, "will rollback") == 0);
  state = product_state_snapshot(101, 101'000);
  assert(state.reminders.size() == 1);
  assert(test_storage::record.find("will rollback") == std::string::npos);
  test_storage::fail_write = false;

  // Persisted old reminders become missed on the first trusted tick, with no
  // audible replay. Repeated ticks retain one record and the weekly plan.
  product_tick_time(1040, 1'900'000'001, true, false);
  assert(product_time_load());
  const auto pending_before = product_state_snapshot(0, 0).pending_reminders;
  assert(pending_before.size() == 1 && pending_before[0].missed);
  assert(product_pending_reminder_hint() == "错过提醒：weekly");
  assert(product_tick_time(1, 1'900'000'001, true, false) == 0);
  assert(product_state_snapshot(1, 1000).pending_reminders.size() == 1);
  assert(product_handle_reminder(reminder));
  assert(product_pending_reminder_hint().empty());
  assert(product_state_snapshot(1, 1000).reminders[0].state == ReminderState::Scheduled);

  const auto valid = test_storage::record;
  for (const auto &bad : {std::string("{\"schema\":2}"), std::string(17 * 1024, 'x')}) {
    test_storage::record = bad;
    assert(!product_time_load());
    assert(!product_time_save(0, 0));
    assert(test_storage::record == bad);
    test_storage::record = valid;
    assert(product_time_load(0));
  }
  test_storage::fail_read = true;
  assert(!product_time_load());
  assert(!product_time_save(0, 0));
  assert(test_storage::record == valid);
  test_storage::fail_read = false;
  assert(product_time_load(0));

  assert(product_request_pomodoro(PomodoroAction::Resume));
  assert(!product_request_pomodoro(PomodoroAction::Start));
  assert(product_state_snapshot(1, 1000).time_operation_pending);
  assert(product_state_snapshot(1, 1000).pomodoro.paused);
  product_poll_pomodoro_request(2000);
  assert(!product_state_snapshot(2000, 0).time_operation_pending);
  assert(!product_state_snapshot(2000, 0).pomodoro.paused);

  // Automatic checkpoints back off, stop after three failures, and remain
  // recoverable by a successful explicit user operation.
  test_storage::fail_write = true;
  auto failed_writes = test_storage::writes;
  product_tick_time(2030, 0, false, false);
  product_tick_time(2031, 0, false, false);
  assert(test_storage::writes == failed_writes + 1);
  product_tick_time(2035, 0, false, false);
  product_tick_time(2050, 0, false, false);
  product_tick_time(9999, 0, false, false);
  assert(test_storage::writes == failed_writes + 3);
  test_storage::fail_write = false;
  assert(product_pomodoro_pause(2051));
  assert(product_state_snapshot(2051, 0).time_storage_error.empty());

  // A bad stored record is surfaced and preserved. No mutation may silently
  // replace it with a fresh default record.
  test_storage::record = "{corrupt";
  assert(!product_time_load());
  state = product_state_snapshot(101, 101'000);
  assert(!state.time_storage_ready);
  assert(!product_time_save(101, 101'000));
  assert(test_storage::record == "{corrupt");
  std::cout << "Time persistence: reboot pause, checkpoints, atomic publication, rollback, missed events, read/schema/size faults and bounded retry passed\n";
}
