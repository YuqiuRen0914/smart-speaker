#include "lunaredu_lunaros/kernel/executor_tasks.h"
#include <cassert>
#include <cstring>
#include <freertos/task.h>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <windows.h>
using namespace lunaredu::lunaros;
namespace {
std::mutex registry_mutex;
std::map<std::string, DWORD> registry, owners;
std::vector<HANDLE> threads;
std::atomic<int> ticks{0};
int create_index = 0, fail_creation = -1;
bool fail_binding = false, fail_activation = false;
struct Start {
  std::string id;
  ManagedTaskFunction fn;
  void *context;
  bool activated;
};
DWORD WINAPI launch(void *opaque) {
  std::unique_ptr<Start> start(static_cast<Start *>(opaque));
  {
    std::lock_guard<std::mutex> lock(registry_mutex);
    registry[start->id] = GetCurrentThreadId();
  }
  if (start->activated)
    start->fn(start->context);
  {
    std::lock_guard<std::mutex> lock(registry_mutex);
    registry.erase(start->id);
  }
  return 0;
}
uint32_t tick(void *) {
  ++ticks;
  return 1;
}
void join() {
  for (HANDLE thread : threads) {
    assert(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    CloseHandle(thread);
  }
  threads.clear();
  assert(registry.empty() && owners.empty() && ticks == 0);
}
} // namespace
int64_t esp_timer_get_time() {
  return static_cast<int64_t>(GetTickCount64()) * 1000;
}
void vTaskDelay(TickType_t delay) { Sleep(delay); }
uint32_t ulTaskNotifyTake(int, TickType_t delay) {
  Sleep(delay);
  return 0;
}
namespace lunaredu::lunaros {
bool create_managed_task(const ManagedTaskDefinition &spec,
                         ManagedTaskFunction fn, void *context,
                         ManagedTaskHandle *, std::string &error) {
  if (create_index++ == fail_creation) {
    error = "create_failed";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(registry_mutex);
    registry[spec.id] = 0;
  }
  HANDLE thread = CreateThread(
      nullptr, 0, launch, new Start{spec.id, fn, context, !fail_activation}, 0,
      nullptr);
  assert(thread);
  threads.push_back(thread);
  return true;
}
ManagedTaskRegistrySnapshot managed_task_registry_snapshot() {
  std::lock_guard<std::mutex> lock(registry_mutex);
  ManagedTaskRegistrySnapshot result;
  size_t i = 0;
  for (const auto &[id, thread] : registry) {
    auto &entry = result.tasks[i++];
    entry.active = true;
    entry.native_handle = thread;
    std::strcpy(entry.id.data(), id.c_str());
  }
  return result;
}
bool bind_service_executor_to_current_task(const char *id, std::string *error) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  if (fail_binding && std::strcmp(id, "network") == 0) {
    *error = "bind_failed";
    return false;
  }
  assert(!owners.count(id));
  owners[id] = GetCurrentThreadId();
  return true;
}
bool unbind_service_executor_from_current_task(const char *id, std::string *) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  assert(owners[id] == GetCurrentThreadId());
  owners.erase(id);
  return true;
}
bool pump_service_executor(const char *id, uint32_t, std::string *) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  assert(owners.size() == 3 && owners[id] == GetCurrentThreadId());
  return true;
}
} // namespace lunaredu::lunaros
int main() {
  static ExecutorTaskGroup group;
  const auto start = [&](std::string &error) {
    create_index = 0;
    return group.start({{{.id = "main"}, "main", tick},
                        {{.id = "network"}, "network", tick},
                        {{.id = "io"}, "io", tick}},
                       error);
  };
  std::string error;
  for (int failed = 0; failed < 3; ++failed) {
    fail_creation = failed;
    assert(!start(error) && error == "create_failed");
    join();
  }
  fail_creation = -1;
  fail_binding = true;
  assert(!start(error) && error == "bind_failed");
  join();
  fail_binding = false;
  fail_activation = true;
  assert(!start(error) && error == "service_executor_bind_timeout");
  join();
  fail_activation = false;
  assert(start(error));
  const auto deadline = GetTickCount64() + 5000;
  while ((!group.healthy() || ticks < 3) && GetTickCount64() < deadline)
    Sleep(1);
  assert(group.healthy() && ticks >= 3);
  assert(!start(error) && error == "executor_tasks_already_started");
  std::cout << "Executor admission/bind/activation failure cleanup, retry and "
               "all-bound barrier passed\n"
            << std::flush;
  // Resident production tasks deliberately live until process/firmware exit.
  ExitProcess(0);
}
