#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "lunaredu_lunaros/kernel/device_test.h"
#include "lunaredu_lunaros/esp_idf/device_test.h"
#include "kernel/tasks/task_registry_internal.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "platform.h"
using namespace lunaredu::lunaros;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); std::abort(); } } while (false)
struct State {
  std::atomic<unsigned> initialized{0}, stopped{0}, ticks{0};
  std::atomic<bool> owns{false}, fail_init{false}, fail_stop{false};
  std::atomic<bool> block{false}, entered{false}, release{false};
  int copied = 0;
};
State states[10];
template<int N> DeviceTestResult init(const void *config) {
  auto &s = states[N]; ++s.initialized; s.owns = true;
  s.copied = *static_cast<const int *>(config);
  return s.fail_init ? DeviceTestResult::Failed : DeviceTestResult::Ok;
}
template<int N> void tick(uint32_t) {
  auto &s = states[N]; CHECK(s.owns); ++s.ticks;
  if (s.block) { s.entered = true; while (!s.release) vTaskDelay(1); }
}
template<int N> DeviceTestResult stop() {
  auto &s = states[N]; ++s.stopped;
  if (s.fail_stop) return DeviceTestResult::Failed;
  s.owns = false; return DeviceTestResult::Ok;
}
const DeviceTestDefinition tests[] = {
  {"test0", init<0>, tick<0>, stop<0>, 1}, {"test1", init<1>, tick<1>, stop<1>, 1},
  {"test2", init<2>, tick<2>, stop<2>, 1}, {"test3", init<3>, tick<3>, stop<3>, 1},
  {"test4", init<4>, tick<4>, stop<4>, 1}, {"test5", init<5>, tick<5>, stop<5>, 1},
  {"test6", init<6>, tick<6>, stop<6>, 1}, {"test7", init<7>, tick<7>, stop<7>, 1},
  {"test8", init<8>, tick<8>, stop<8>, 1}, {"test9", init<9>, tick<9>, stop<9>, 1},
};
template<class Predicate> void wait_for(Predicate ready) {
  const auto deadline = esp_timer_get_time() + 3000000;
  while (!ready()) { CHECK(esp_timer_get_time() < deadline); vTaskDelay(1); }
}
bool admitted() { return managed_task_registry_internal().contains("device_test"); }
esp_err_t native_init_error = ESP_OK, native_stop_error = ESP_OK;
esp_err_t native_initialize(const void *) { return native_init_error; }
void native_tick(uint32_t) {}
esp_err_t native_shutdown() { return native_stop_error; }
const esp_idf::DeviceTest<native_initialize, native_tick, native_shutdown> native_test{"native", 20};
int main() {
  std::string error; int config = 42;
  CHECK(managed_task_registry_snapshot().revision == 0);
  CHECK(stop_device_test(tests[0]) == DeviceTestResult::Ok);
  CHECK(start_device_test(tests[0], nullptr) == DeviceTestResult::InvalidArgument);
  CHECK(states[0].initialized == 0);
  CHECK(start_device_test(tests[0], &config) == DeviceTestResult::Ok);
  CHECK(managed_task_registry_snapshot().revision > 0);
  config = 17; CHECK(states[0].copied == 42);
  wait_for([] { return states[0].ticks > 0; });
  CHECK(start_device_test(tests[0], &config) == DeviceTestResult::InvalidState);
  CHECK(states[0].initialized == 1 && task_creations == 1 && admitted());
  CHECK(stop_device_test(tests[0]) == DeviceTestResult::Ok && !admitted());
  auto stopped_ticks = states[0].ticks.load(); vTaskDelay(30);
  CHECK(states[0].ticks == stopped_ticks && !states[0].owns);
  CHECK(stop_device_test(tests[0]) == DeviceTestResult::Ok && states[0].stopped == 1);
  const auto before_shared = task_creations.load();
  for (unsigned i = 0; i < 8; ++i) CHECK(start_device_test(tests[i], &config) == DeviceTestResult::Ok);
  CHECK(task_creations == before_shared + 1);
  CHECK(start_device_test(tests[8], &config) == DeviceTestResult::NoMemory && states[8].initialized == 0);
  CHECK(stop_device_test(tests[3]) == DeviceTestResult::Ok && admitted());
  CHECK(start_device_test(tests[8], &config) == DeviceTestResult::Ok);
  for (unsigned i = 0; i <= 8; ++i) CHECK(stop_device_test(tests[i]) == DeviceTestResult::Ok);
  CHECK(!admitted());
  states[9].fail_init = true;
  CHECK(start_device_test(tests[9], &config) == DeviceTestResult::Failed);
  CHECK(states[9].stopped == 1 && !states[9].owns && !admitted());
  states[9].fail_stop = true;
  CHECK(start_device_test(tests[9], &config) == DeviceTestResult::Failed && states[9].owns);
  CHECK(start_device_test(tests[9], &config) == DeviceTestResult::InvalidState);
  CHECK(stop_device_test(tests[9]) == DeviceTestResult::Failed);
  states[9].fail_stop = false; states[9].fail_init = false;
  CHECK(stop_device_test(tests[9]) == DeviceTestResult::Ok && !states[9].owns);
  fail_task_creation = true;
  const auto stopped_before = states[0].stopped.load();
  CHECK(start_device_test(tests[0], &config) == DeviceTestResult::NoMemory);
  CHECK(states[0].stopped == stopped_before + 1 && !states[0].owns && !admitted());
  fail_task_creation = false;
  CHECK(start_device_test(tests[0], &config) == DeviceTestResult::Ok);
  states[0].fail_stop = true;
  CHECK(stop_device_test(tests[0]) == DeviceTestResult::Failed && !admitted() && states[0].owns);
  CHECK(start_device_test(tests[0], &config) == DeviceTestResult::InvalidState);
  CHECK(start_device_test(tests[1], &config) == DeviceTestResult::Ok);
  CHECK(stop_device_test(tests[1]) == DeviceTestResult::Ok);
  states[0].fail_stop = false; CHECK(stop_device_test(tests[0]) == DeviceTestResult::Ok);
  states[0].block = true; states[0].entered = false; states[0].release = false;
  CHECK(start_device_test(tests[0], &config) == DeviceTestResult::Ok);
  wait_for([] { return states[0].entered.load(); });
  std::atomic<bool> stop_returned{false};
  background([](void *opaque) {
    CHECK(stop_device_test(tests[0]) == DeviceTestResult::Ok);
    static_cast<std::atomic<bool> *>(opaque)->store(true);
  }, &stop_returned);
  vTaskDelay(30); CHECK(!stop_returned && states[0].owns);
  states[0].release = true; wait_for([&] { return stop_returned.load(); });
  CHECK(!states[0].owns && !admitted()); states[0].block = false;
  for (unsigned i = 0; i < 20; ++i) {
    CHECK(start_device_test(tests[0], &config) == DeviceTestResult::Ok);
    CHECK(stop_device_test(tests[0]) == DeviceTestResult::Ok && !admitted());
  }
  CHECK(native_test.start(nullptr) == ESP_ERR_INVALID_ARG);
  native_init_error = 0x777; native_stop_error = 0x778;
  CHECK(native_test.start(&config) == 0x777); // init error survives failed rollback
  CHECK(native_test.stop() == 0x778);
  native_stop_error = ESP_OK; CHECK(native_test.stop() == ESP_OK);
  native_init_error = ESP_OK; CHECK(native_test.start(&config) == ESP_OK);
  CHECK(native_test.start(&config) == ESP_ERR_INVALID_STATE);
  CHECK(native_test.stop() == ESP_OK);
  auto budget = ManagedTaskBudget{}; budget.maximum_internal_stack_bytes = 1024;
  CHECK(setup_managed_task_registry(budget, error));
  CHECK(start_device_test(tests[0], &config) == DeviceTestResult::NoMemory && !states[0].owns && !admitted());
  CHECK(managed_task_registry_snapshot().budget.maximum_internal_stack_bytes == 1024);
  std::puts("PASS: config lifetime, duplicate start, shared worker/capacity, init rollback, cleanup retry, task failure/budget, stop during I/O, idempotent stop, immediate restart");
}
