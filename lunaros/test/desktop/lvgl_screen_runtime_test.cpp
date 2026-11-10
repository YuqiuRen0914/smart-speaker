// Real LVGL and the complete production runtime; only RTOS/hardware are faked.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <lvgl.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <thread>
#endif
static bool fail_touch = false;
static lv_indev_t *create_touch() { return fail_touch ? nullptr : lv_indev_create(); }
#define lv_indev_create create_touch
#include "../../src/desktop/local/lvgl_screen_runtime.cpp"
#undef lv_indev_create
#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::abort(); \
} } while (false)
namespace lunaredu::lunaros {
bool create_managed_task(const ManagedTaskDefinition &, ManagedTaskFunction fn,
                         void *, ManagedTaskHandle *handle, std::string &error) {
  if (runtime_test::fail_worker) { error = "injected"; return false; }
  runtime_test::worker = fn;
  ++runtime_test::workers_started;
  *handle = reinterpret_cast<ManagedTaskHandle>(1);
  return true;
}
}
using namespace lunaredu::desktop;
static int draws = 0;
static int fail_draw = -1;
static bool draw(void *, uint16_t, uint16_t, uint16_t, uint16_t,
                 const uint8_t *, LvglScreenPixelFormat, std::string &error) {
  runtime_test::time_us += 100;
  if (++draws == fail_draw) { error = "injected_transfer_failure"; return false; }
  return true;
}
static LvglScreenRuntimeConfig config(uint16_t rows = 24) {
  LvglScreenRuntimeConfig c;
  c.draw_buffer_rows = rows;
  c.surface.width = 240; c.surface.height = 240; c.surface.draw_bitmap = draw;
  c.read_touch = [](uint16_t *, uint16_t *, bool *, void *) { return false; };
  return c;
}
static void refresh() {
  lv_obj_invalidate(lv_screen_active());
  runtime_test::time_us += 20000;
  lvgl_screen_runtime_handler();
  runtime_test::pump(); // The final asynchronous tile finishes after LVGL returns.
}
static void check_status() {
  const auto status = lvgl_screen_runtime_status();
  CHECK(status.initialized && status.flush_pending);
  CHECK(lvgl_screen_runtime_ready());
}
#if defined(_WIN32)
static DWORD WINAPI read_status(void *) { check_status(); return 0; }
#endif
static void check_diagnostics_while_owner_locked() {
  std::lock_guard<std::recursive_mutex> lock(g_mutex);
#if defined(_WIN32)
  HANDLE reader = CreateThread(nullptr, 0, read_status, nullptr, 0, nullptr);
  CHECK(reader != nullptr);
  CHECK(WaitForSingleObject(reader, 1000) == WAIT_OBJECT_0);
  CloseHandle(reader);
#else
  std::thread reader(check_status);
  reader.join();
#endif
}
int main(int argc, char **argv) {
  CHECK(argc == 2);
  const std::string mode = argv[1];
  if (mode == "touch_failure" || mode == "worker_failure") {
    fail_touch = mode == "touch_failure";
    runtime_test::fail_worker = mode == "worker_failure";
    CHECK(!setup_lvgl_screen_runtime(config()));
    auto status = lvgl_screen_runtime_status();
    CHECK(!status.initialized && !status.touch_input);
    CHECK(status.last_error == (fail_touch ? "lvgl_touch_create_failed" :
                                             "lvgl_flush_task_create_failed:injected"));
    CHECK(runtime_test::buffers == 0 && runtime_test::sync_allocations == 0);
    CHECK(runtime_test::workers_started == 0);
    CHECK(lv_display_get_default() == nullptr);
    CHECK(lv_indev_get_next(nullptr) == nullptr);
    fail_touch = false; runtime_test::fail_worker = false;
    CHECK(setup_lvgl_screen_runtime(config()));
    status = lvgl_screen_runtime_status();
    CHECK(status.initialized && status.touch_input && status.last_error.empty());
    CHECK(runtime_test::workers_started == 1);
    CHECK(setup_lvgl_screen_runtime(config()));
    CHECK(runtime_test::workers_started == 1);
    refresh();
    CHECK(lvgl_screen_runtime_status().refresh_count == 1);
  } else if (mode == "metrics" || mode == "transfer_failure" || mode == "queue_failure") {
    CHECK(setup_lvgl_screen_runtime(config()));
    if (mode == "transfer_failure") fail_draw = 3;
    if (mode == "queue_failure") runtime_test::reject_queue = true;
    refresh();
    auto status = lvgl_screen_runtime_status();
    CHECK(!status.flush_pending);
    if (mode == "metrics") {
      CHECK(status.flush_count == 10 && status.refresh_count == 1);
      CHECK(status.full_refresh_count == 1 && status.last_refresh_pixels == 240 * 240);
      CHECK(status.last_refresh_width == 240 && status.last_refresh_height == 240);
      CHECK(status.last_refresh_us == 1000 && status.last_flush_us == 100);
      CHECK(status.total_flush_us == 1000 && status.total_refresh_us == 1000);
    } else {
      CHECK(status.refresh_count == 0 && status.full_refresh_count == 0);
      CHECK(status.flush_fail_count == (mode == "queue_failure" ? 10u : 1u));
      CHECK(!status.last_error.empty());
      fail_draw = -1; runtime_test::reject_queue = false;
      refresh();
      status = lvgl_screen_runtime_status();
      CHECK(status.refresh_count == 1 && status.full_refresh_count == 1);
      CHECK(status.last_error.empty());
    }
  } else if (mode == "timeout" || mode == "timeout_last") {
    CHECK(setup_lvgl_screen_runtime(config(mode == "timeout_last" ? 240 : 24)));
    runtime_test::worker_blocked = true;
    uint8_t *const owned_buffer = g_draw_buffer;
    try {
      refresh(); // Partial buffers wait inside the actual LVGL handler.
      CHECK(mode == "timeout_last");
      check_diagnostics_while_owner_locked();
      runtime_test::time_us += 3000000;
      lvgl_screen_runtime_handler(); // A final tile is checked on the next tick.
      CHECK(false);
    } catch (const runtime_test::Restart &) {
      CHECK(runtime_test::restarts == 1);
      CHECK(lvgl_screen_runtime_status().last_error == "lvgl_flush_wait_timeout");
      CHECK(g_flush_pending && g_draw_buffer == owned_buffer);
      CHECK(draws == 0 && runtime_test::buffers == 1);
      check_diagnostics_while_owner_locked();
    }
  } else { CHECK(false); }
  std::printf("PASS display runtime %s\n", mode.c_str());
}
