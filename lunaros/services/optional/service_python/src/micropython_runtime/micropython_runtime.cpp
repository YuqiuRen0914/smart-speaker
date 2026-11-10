#include "kernel/task_signal.h"
#include "lunaros_micropython_runtime.h"
#include "lunaros_micropython_runtime_config.h"
#include "start_attempt.h"
#include "app/execution/execution_engine_lease.h"

#include <atomic>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "sdkconfig.h"

extern "C" {
#include "port/micropython_embed.h"
}

#include <esp_log.h>
#include "kernel/runtime.h"
#include "platform/text.h"
#include "kernel/managed_task.h"
#include "kernel/service_executor.h"

#include "micropython_execution.h"
#include "python_application_vm.h"
#include "micropython_application_owner.h"
#include "lunaros_micropython_native_modules.h"
#include "lunaros_micropython_config.h"

#include "micropython_runtime_context.h"

#include "micropython_engine_hooks.h"
#include "micropython_stdout_hook.h"

using namespace lunaredu::lunaros;

namespace {

enum class MicroPythonVmCommandKind : uint8_t {
  ExecuteScript,
  ExecuteRepl,
};

struct MicroPythonVmCommand {
  MicroPythonVmCommandKind kind = MicroPythonVmCommandKind::ExecuteScript;
  uint32_t execution_id = 0;
  uint32_t repl_session_id = 0;
  std::string source;
  std::string program;
  uint32_t maximum_duration_ms = 0;
};

// The VM object heap is ordinary byte-addressable data. Allocate it explicitly
// in PSRAM: EXT_RAM_BSS_ATTR silently becomes internal .bss when the global
// external-BSS Kconfig option is off, consuming 64 KiB of scarce internal RAM.
// The VM task stack remains internal for flash-safe native bindings.
uint8_t *g_micropython_heap = nullptr;

bool ensure_vm_heap() {
  if (g_micropython_heap != nullptr) return true;
#if CONFIG_SPIRAM
  constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif
  g_micropython_heap = static_cast<uint8_t *>(
      heap_caps_aligned_alloc(16, kMicroPythonHeapBytes, caps));
  return g_micropython_heap != nullptr;
}

std::mutex g_state_mutex;
MicroPythonRuntimeStatus g_status;
MicroPythonLogEntry *g_logs = nullptr;
size_t g_log_write_index = 0;
uint32_t g_log_seq = 0;
uint32_t g_next_execution_id = 0;
uint32_t g_repl_session_id = 0;
bool g_command_pending = false;
bool g_application_owned = false;
void *g_owner_stack_top = nullptr;
std::string g_stdout_line_buffer;

QueueHandle_t g_vm_command_queue = nullptr;
SemaphoreHandle_t g_vm_ready = nullptr;
TaskHandle_t g_vm_task_handle = nullptr;
std::atomic<bool> g_setup_started{false};
lunaredu::lunaros::apps::ExecutionEngineLease g_engine_lease;
// Executor registration owns a persistent OS queue. Keep it across failed VM
// startup attempts: registering the same executor twice is rejected by LunarOS.
bool g_executor_registered = false;
std::atomic<bool> g_vm_initialized{false};
KernelRuntime *g_owner_runtime = nullptr; // stable for the managed VM lifetime
std::atomic<bool> g_idle_shutdown_requested{false};
std::atomic<uint32_t> g_active_execution_id{0};
std::atomic<uint32_t> g_cancel_through_execution_id{0};
std::atomic<int64_t> g_background_deadline_us{0};
std::atomic<bool> g_background_timed_out{false};
uint32_t g_background_execution_id = 0; // guarded by g_state_mutex
uint64_t g_startup_revision = 0; // never reset across VM instances
std::array<MicroPythonExecutionReceipt, 8> g_execution_receipts;
size_t g_execution_receipt_index = 0;

bool ensure_log_storage() {
  if (g_logs != nullptr) {
    return true;
  }
  void *storage = heap_caps_malloc(
      sizeof(MicroPythonLogEntry) * kMicroPythonMaxLogs,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (storage == nullptr) {
    storage = heap_caps_malloc(sizeof(MicroPythonLogEntry) * kMicroPythonMaxLogs, MALLOC_CAP_8BIT);
  }
  if (storage == nullptr) return false;
  g_logs = static_cast<MicroPythonLogEntry *>(storage);
  for (size_t index = 0; index < kMicroPythonMaxLogs; ++index) {
    new (&g_logs[index]) MicroPythonLogEntry();
  }
  return true;
}

uint32_t now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void set_state_locked(const char *state) {
  g_status.state = state != nullptr ? state : "unknown";
}

void add_log_locked(const char *level, const std::string &text) {
  if (g_logs == nullptr) {
    return;
  }
  MicroPythonLogEntry &entry = g_logs[g_log_write_index];
  entry.seq = ++g_log_seq;
  entry.t_ms = now_ms();
  entry.level = level != nullptr ? level : "info";
  entry.text = text;
  entry.execution_id = g_status.execution_id;
  g_log_write_index = (g_log_write_index + 1) % kMicroPythonMaxLogs;
  g_status.log_seq = g_log_seq;
}

void add_log(const char *level, const std::string &text) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  add_log_locked(level, text);
}

void set_rejected_error_locked(const std::string &error) {
  if (!g_status.running) {
    g_status.last_error = error;
    set_state_locked("error");
  }
  add_log_locked("error", error);
}

void append_stdout_log_fragment_locked(const std::string &text) {
  if (text.empty()) {
    return;
  }

  for (const char ch : text) {
    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      const std::string line =
          lunaredu::lunaros::text::trim_copy(g_stdout_line_buffer);
      g_stdout_line_buffer.clear();
      if (!line.empty()) {
        add_log_locked("stdout", line);
      }
      continue;
    }

    g_stdout_line_buffer += ch;
    if (g_stdout_line_buffer.length() >= 160) {
      add_log_locked("stdout", g_stdout_line_buffer);
      g_stdout_line_buffer.clear();
    }
  }
}

void request_cancel_through(uint32_t execution_id) {
  uint32_t observed =
      g_cancel_through_execution_id.load(std::memory_order_relaxed);
  while (observed < execution_id &&
         !g_cancel_through_execution_id.compare_exchange_weak(
             observed, execution_id, std::memory_order_release,
             std::memory_order_relaxed)) {
  }
}

bool command_cancelled(uint32_t execution_id) {
  return g_cancel_through_execution_id.load(std::memory_order_acquire) >=
         execution_id;
}

bool reset_vm_on_owner(void *stack_top) {
  if (!g_vm_initialized.load(std::memory_order_acquire)) {
    return false;
  }

  retire_micropython_native_modules();mp_embed_deinit();
  mp_embed_init(g_micropython_heap, kMicroPythonHeapBytes, stack_top);
  setup_micropython_native_modules();
  return true;
}

void finish_vm_command(const MicroPythonVmCommand &command,
                       const MicroPythonExecutionOutcome &raw_outcome) {
  std::lock_guard<std::mutex> lock(g_state_mutex);

  MicroPythonExecutionOutcome outcome = raw_outcome;
  if (command.maximum_duration_ms && g_background_timed_out.load(std::memory_order_acquire)) {
    outcome.state = MicroPythonExecutionState::Failed;
    outcome.error = "python_execution_timed_out";
  }
  if (command_cancelled(command.execution_id)) {
    outcome.state = MicroPythonExecutionState::Aborted;
    outcome.error = "script_stopped";
  }

  if (g_status.execution_id != command.execution_id) {
    g_command_pending = false;
    return;
  }

  g_command_pending = false;
  g_status.running = false;
  g_status.stop_requested = false;
  g_status.completed_at_ms = now_ms();
  g_execution_receipts[g_execution_receipt_index++ % g_execution_receipts.size()] = {
    command.execution_id, true, outcome.state == MicroPythonExecutionState::Completed,
    outcome.state == MicroPythonExecutionState::Aborted, outcome.error};

  switch (outcome.state) {
  case MicroPythonExecutionState::Completed:
    g_status.last_error.clear();
    set_state_locked(command.kind == MicroPythonVmCommandKind::ExecuteRepl
                         ? "repl"
                         : (g_status.entered ? "idle" : "embedded_ready"));
    add_log_locked("info", "done:" + command.program);
    break;

  case MicroPythonExecutionState::Aborted:
    g_status.repl_active = false;
    g_status.repl_mode.clear();
    g_status.current_program.clear();
    g_status.last_error.clear();
    set_state_locked("stopped");
    add_log_locked("info", "stopped:" + command.program);
    break;

  case MicroPythonExecutionState::Failed:
    g_status.last_error = outcome.error.empty() ? "python_exception"
                                                : outcome.error;
    set_state_locked(command.kind == MicroPythonVmCommandKind::ExecuteRepl
                         ? "repl_error"
                         : "error");
    add_log_locked("error", "failed:" + command.program + ":" +
                                g_status.last_error);
    break;
  }
}

void execute_vm_command_on_owner(const MicroPythonVmCommand &command,
                                 void *stack_top,
                                 uint32_t &owner_repl_session_id) {
  const bool repl_command =
      command.kind == MicroPythonVmCommandKind::ExecuteRepl;
  const bool needs_reset =
      !repl_command || owner_repl_session_id != command.repl_session_id;

  if (needs_reset && !reset_vm_on_owner(stack_top)) {
    finish_vm_command(
        command,
        {MicroPythonExecutionState::Failed, "runtime_reset_failed"});
    return;
  }
  owner_repl_session_id = repl_command ? command.repl_session_id : 0;

  if (command_cancelled(command.execution_id)) {
    finish_vm_command(
        command,
        {MicroPythonExecutionState::Aborted, "script_stopped"});
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_status.execution_id == command.execution_id) {
      set_state_locked("running");
      add_log_locked("info", "run:" + command.program);
    }
  }

  clear_micropython_vm_abort_request();
  g_background_timed_out.store(false, std::memory_order_release);
  g_background_deadline_us.store(command.maximum_duration_ms
      ? esp_timer_get_time() + int64_t(command.maximum_duration_ms) * 1000 : 0,
      std::memory_order_release);
  g_active_execution_id.store(command.execution_id, std::memory_order_release);
  if (command_cancelled(command.execution_id)) {
    g_active_execution_id.store(0, std::memory_order_release);
    g_background_deadline_us.store(0, std::memory_order_release);
    clear_micropython_vm_abort_request();
    finish_vm_command(
        command,
        {MicroPythonExecutionState::Aborted, "script_stopped"});
    return;
  }

  const MicroPythonExecutionOutcome outcome =
      execute_micropython_source_on_owner(command.source);
  g_background_deadline_us.store(0, std::memory_order_release);
  g_active_execution_id.store(0, std::memory_order_release);
  clear_micropython_vm_abort_request();
  finish_vm_command(command, outcome);
}

extern "C" int lunaros_micropython_take_vm_abort_request();

void signal_vm_ready() {
  // The setup caller owns this semaphore until its wait finishes. A timed-out
  // caller clears the borrowed marker under the same lock before deleting it.
  std::lock_guard<std::mutex> lock(g_state_mutex);
  if (g_vm_ready != nullptr) xSemaphoreGive(g_vm_ready);
}

void wait_for_vm_startup(SemaphoreHandle_t ready) {
  const auto result = xSemaphoreTake(ready, pdMS_TO_TICKS(5000));
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    // An old waiter may outlive its VM. It must not clear a newer startup's
    // marker or overwrite that owner's status with an obsolete timeout.
    if (g_vm_ready == ready) {
      g_vm_ready = nullptr;
      if (result != pdPASS && !g_status.initialized) {
        g_status.last_error = "runtime_init_timeout";
        set_state_locked("error");
        add_log_locked("error", g_status.last_error);
      }
    }
  }
  vSemaphoreDelete(ready);
}

bool begin_vm_retirement() {
  if (!g_idle_shutdown_requested.load(std::memory_order_acquire)) return false;
  std::lock_guard<std::mutex> lock(g_state_mutex);
  // Admission reserves this flag before releasing the state lock, including
  // the interval before xQueueSend. Leave may cancel that command, but cannot
  // destroy its queue until handoff and owner-side completion acknowledge it.
  if (!g_idle_shutdown_requested.load(std::memory_order_acquire) ||
      g_command_pending) return false;
  g_status.initialized = false;
  return true;
}

void micropython_vm_task(void *parameter) {
  const MicroPythonEngineHooks hooks{
      nullptr,
      [](void *) -> int {
        const auto deadline = g_background_deadline_us.load(std::memory_order_acquire);
        if (deadline && esp_timer_get_time() >= deadline) {
          g_background_timed_out.store(true, std::memory_order_release);
          return 1;
        }
        return lunaredu::micropython::python_application_budget_abort() || lunaros_micropython_take_vm_abort_request();
      },
      [](void *) { vTaskDelay(1); },
      [](void *, const char *text, size_t length) {
        micropython_runtime_append_stdout(text, length);
      }};
  micropython_engine_set_hooks(&hooks);
  auto &runtime = *static_cast<lunaredu::lunaros::KernelRuntime *>(parameter);

  std::string executor_error;
  if (!runtime.bind_executor(lunaredu::micropython::kRuntimeExecutorId,
                             &executor_error)) {
    ESP_LOGE("micropython", "failed to bind VM service executor: %s",
         executor_error.c_str());
    // No VM exists and command admission is still closed. This attempt owns
    // its heap/command queue, but never another task's executor binding.
    micropython_engine_set_hooks(nullptr);
    heap_caps_free(g_micropython_heap);
    g_micropython_heap = nullptr;
    vQueueDelete(g_vm_command_queue);
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_vm_command_queue = nullptr;
    g_vm_task_handle = nullptr;
    g_owner_stack_top = nullptr;
    g_status.initialized = false;
    g_status.running = false;
    g_status.stop_requested = false;
    g_status.last_error = "runtime_service_executor_bind_failed:" + executor_error;
    set_state_locked("error");
    add_log_locked("error", g_status.last_error);
    g_vm_initialized.store(false, std::memory_order_release);
    g_engine_lease.release();
    g_setup_started.store(false, std::memory_order_release);
    // Publish completion only after cleanup. The caller owns this semaphore;
    // a timed-out caller already detached it under this same lock.
    if (g_vm_ready != nullptr) xSemaphoreGive(g_vm_ready);
    g_vm_ready = nullptr;
    return; // The managed wrapper releases this task's stack and admission.
  }

  int stack_top_marker = 0;
  g_owner_stack_top = &stack_top_marker;
  mp_embed_init(g_micropython_heap, kMicroPythonHeapBytes,
                &stack_top_marker);
  setup_micropython_native_modules();
  g_vm_initialized.store(true, std::memory_order_release);

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_status.available = true;
    g_status.initialized = true;
    g_status.last_error.clear();
    set_state_locked("embedded_ready");
    add_log_locked("info", "runtime_initialized");
  }

  ESP_LOGI("micropython", "runtime initialized heap=%u stack=%u queue=%u",
       static_cast<unsigned>(kMicroPythonHeapBytes),
       static_cast<unsigned>(kMicroPythonTaskStackBytes),
       static_cast<unsigned>(kMicroPythonCommandQueueDepth));

  signal_vm_ready();

  uint32_t owner_repl_session_id = 0;
  for (;;) {
    runtime.pump_executor(lunaredu::micropython::kRuntimeExecutorId,
                          100);

    MicroPythonVmCommand *command = nullptr;
    while (xQueueReceive(g_vm_command_queue, &command, 0) == pdPASS) {
      if (command == nullptr) {
        continue;
      }
      execute_vm_command_on_owner(*command, &stack_top_marker,
                                  owner_repl_session_id);
      delete command;
      command = nullptr;
    }
    if (begin_vm_retirement()) {
      // Drain admitted executor operations before unbinding. New VM work is
      // rejected while shutdown is requested; never delete a live owner task.
      if (!runtime.unbind_executor(lunaredu::micropython::kRuntimeExecutorId))
        continue;
      retire_micropython_native_modules();mp_embed_deinit();
      micropython_engine_set_hooks(nullptr);
      heap_caps_free(g_micropython_heap);
      g_micropython_heap = nullptr;
      vQueueDelete(g_vm_command_queue);
      g_owner_stack_top = nullptr;
      {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_vm_command_queue = nullptr;
        // The setup caller may still be returning from xSemaphoreTake. It
        // owns deletion; retirement only drops this owner's borrowed marker.
        g_vm_ready = nullptr;
        g_vm_task_handle = nullptr;
        g_status.initialized = false;
        g_status.running = false;
        g_status.stop_requested = false;
        set_state_locked("vm_on_demand");
        g_vm_initialized.store(false, std::memory_order_release);
      }
      // No VM, command queue or engine hook remains at this point. A timeout
      // that never reaches here must keep the admission slot closed.
      g_engine_lease.release();
      g_setup_started.store(false, std::memory_order_release);
      // Returning lets the managed task wrapper release its stack and budget.
      return;
    }
  }
}

void wake_vm_for_queued_command() {
  // A fast command may retire the VM before the producer reaches this wake.
  // Keep the task's ownership marker locked until the notification completes.
  std::lock_guard<std::mutex> lock(g_state_mutex);
  if (g_vm_task_handle != nullptr) {
    lunaredu::lunaros::task_signal::notify(g_vm_task_handle);
  }
}

bool enqueue_source(const std::string &source, const std::string &program,
                    bool repl, std::string &error, uint32_t maximum_duration_ms = 0,
                    uint32_t *admitted_id = nullptr) {
  error.clear();
  if (source.empty()) {
    error = repl ? "empty_repl_input" : "empty_source";
    std::lock_guard<std::mutex> lock(g_state_mutex);
    set_rejected_error_locked(error);
    return false;
  }

  MicroPythonVmCommand *command =
      new (std::nothrow) MicroPythonVmCommand();
  if (command == nullptr) {
    error = "command_allocation_failed";
    std::lock_guard<std::mutex> lock(g_state_mutex);
    set_rejected_error_locked(error);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (!g_status.initialized || g_vm_command_queue == nullptr) {
      error = "runtime_not_initialized";
    } else if (g_idle_shutdown_requested) {
      error = "runtime_shutting_down";
    } else if (g_application_owned) {
      error = "application_owns_python_vm";
    } else if (maximum_duration_ms && (g_status.entered || g_status.repl_active)) {
      error = "foreground_owns_python_vm";
    } else if (!maximum_duration_ms && !g_status.entered) {
      error = "runtime_not_entered";
    } else if (g_status.running || g_command_pending) {
      error = "runtime_busy";
    } else if (repl && !g_status.repl_active) {
      error = "repl_not_active";
    }

    if (!error.empty()) {
      set_rejected_error_locked(error);
      delete command;
      return false;
    }

    command->kind = repl ? MicroPythonVmCommandKind::ExecuteRepl
                         : MicroPythonVmCommandKind::ExecuteScript;
    command->execution_id = ++g_next_execution_id;
    command->repl_session_id = g_repl_session_id;
    command->source = source;
    command->program = program;
    command->maximum_duration_ms = maximum_duration_ms;

    g_command_pending = true;
    g_status.running = true;
    g_status.stop_requested = false;
    g_status.execution_id = command->execution_id;
    g_background_execution_id = maximum_duration_ms ? command->execution_id : 0;
    g_status.current_program = program;
    g_status.last_error.clear();
    g_status.started_at_ms = now_ms();
    g_status.completed_at_ms = 0;
    set_state_locked("queued");
    add_log_locked("info", "queued:" + program);
  }

  const uint32_t queued_id = command->execution_id;
  MicroPythonVmCommand *queued_command = command;
  if (xQueueSend(g_vm_command_queue, &queued_command, pdMS_TO_TICKS(100)) !=
      pdPASS) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_status.execution_id == command->execution_id) {
      g_command_pending = false;
      g_status.running = false;
      g_status.last_error = "runtime_queue_full";
      set_state_locked("error");
      add_log_locked("error", g_status.last_error);
    }
    error = "runtime_queue_full";
    delete command;
    return false;
  }

  wake_vm_for_queued_command();
  if (admitted_id) *admitted_id = queued_id;
  return true;
}


} // namespace

bool acquire_micropython_application_on_owner(std::string &error) {
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if(g_idle_shutdown_requested || !g_vm_initialized || !g_owner_stack_top){error="runtime_not_initialized";return false;}
    if(g_application_owned||g_status.entered||g_status.running||g_status.repl_active||g_command_pending){error="Python VM is busy with a script, REPL or application";return false;}
    g_application_owned=true;set_state_locked("application");
  }
  clear_micropython_vm_abort_request();
  retire_micropython_native_modules();mp_embed_deinit();mp_embed_init(g_micropython_heap,kMicroPythonHeapBytes,g_owner_stack_top);
  // Application packages receive only lunaros_app_v1. Product hardware/native
  // modules are restored on the next script/REPL VM startup or reset.
  return true;
}
void release_micropython_application_on_owner() {
  clear_micropython_vm_abort_request();
  std::lock_guard<std::mutex> lock(g_state_mutex);
  g_application_owned=false;set_state_locked("embedded_ready");
}

MicroPythonStartupClaim micropython_runtime_startup_claim() {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  // Zero remains the invalid token. Exhaustion fails closed, rather than
  // allowing a stale claim to alias a subsequent preparation.
  if (g_startup_revision == UINT64_MAX) return {};
  return {++g_startup_revision};
}

bool micropython_prepare_foreground(std::string &error) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  error.clear();
  if (g_idle_shutdown_requested.load(std::memory_order_acquire)) {
    if (g_setup_started.load(std::memory_order_acquire) ||
        g_vm_initialized.load(std::memory_order_acquire) ||
        (g_owner_runtime && g_owner_runtime->task_active("micropython_vm"))) {
      error = "python_retirement_pending";
      return false;
    }
    // A completed stop keeps its tombstone until the next admission. Only
    // actual owner release, not completion acknowledgement, permits reuse.
    g_idle_shutdown_requested.store(false, std::memory_order_release);
  }
  if (g_application_owned || g_status.running || g_status.repl_active ||
      g_command_pending) {
    error = "python_runtime_in_use";
    return false;
  }
  g_status.entered = true;
  return true;
}

bool micropython_attach_background_foreground(uint32_t execution_id,std::string &error) {
  std::lock_guard<std::mutex> lock(g_state_mutex);error.clear();
  if(!execution_id || g_status.execution_id!=execution_id || g_background_execution_id!=execution_id){error="python_execution_changed";return false;}
  if(g_application_owned || g_status.repl_active || g_command_pending ||
      g_idle_shutdown_requested || !g_status.initialized){error="python_foreground_attach_unavailable";return false;}
  g_status.entered=true;return true;
}

bool micropython_retire_failed_startup(MicroPythonStartupClaim claim,
                                      std::string &error) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  error.clear();
  if (!claim || claim.revision != g_startup_revision) return true;
  // A superseding script/app/REPL owns its own lifetime. A rollback never
  // cancels it, even if it has already completed by the time we poll.
  if (g_application_owned || g_status.entered || g_status.running ||
      g_status.repl_active || g_command_pending) return true;
  if (g_setup_started.load(std::memory_order_acquire)) {
    g_idle_shutdown_requested.store(true, std::memory_order_release);
    if (g_vm_task_handle) lunaredu::lunaros::task_signal::notify(g_vm_task_handle);
  }
  if (g_setup_started.load(std::memory_order_acquire) ||
      (g_owner_runtime && g_owner_runtime->task_active("micropython_vm"))) {
    error = "python_startup_retirement_pending";
    return false;
  }
  return true;
}

bool micropython_retire_background_execution(uint32_t execution_id,
                                            std::string &error) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  error.clear();
  if (!execution_id || g_status.execution_id != execution_id ||
      g_background_execution_id != execution_id) {
    error = "python_execution_changed";
    return false;
  }
  if (g_application_owned || g_status.entered || g_status.running ||
      g_status.repl_active || g_command_pending) {
    error = "python_runtime_in_use";
    return false;
  }
  if (g_vm_initialized.load(std::memory_order_acquire)) {
    g_idle_shutdown_requested.store(true, std::memory_order_release);
    if (g_vm_task_handle) lunaredu::lunaros::task_signal::notify(g_vm_task_handle);
  }
  if (g_vm_initialized.load(std::memory_order_acquire) ||
      (g_owner_runtime && g_owner_runtime->task_active("micropython_vm"))) {
    error = "python_retirement_pending"; return false;
  }
  return true;
}

bool micropython_shutdown_idle(KernelRuntime &runtime, std::string &error) {
  error.clear();
  bool on_owner = false;
  {
    // Admission and shutdown share the state lock. Do not queue a stop behind
    // user code: a lifecycle timeout must bound the actual wait, not admission.
    std::lock_guard<std::mutex> lock(g_state_mutex);
    on_owner = g_vm_task_handle && xTaskGetCurrentTaskHandle() == g_vm_task_handle;
    if (g_vm_initialized.load(std::memory_order_acquire)) {
      if (g_application_owned || g_status.entered || g_status.running ||
          g_status.repl_active || g_command_pending) {
        error = "python_runtime_in_use";
        return false;
      }
      g_idle_shutdown_requested.store(true, std::memory_order_release);
      if (g_vm_task_handle) lunaredu::lunaros::task_signal::notify(g_vm_task_handle);
    }
  }
  if (on_owner) return true;
  const auto deadline = esp_timer_get_time() + 1000000;
  do {
    if (!runtime.task_active("micropython_vm")) return true;
    vTaskDelay(pdMS_TO_TICKS(1));
  } while (esp_timer_get_time() < deadline);
  error = "python_runtime_shutdown_timeout";
  return false;
}

bool micropython_shutdown_runtime(KernelRuntime &runtime, std::string &error) {
  error.clear();
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_application_owned) { error = "python_application_still_active"; return false; }
    if (g_vm_task_handle && xTaskGetCurrentTaskHandle() == g_vm_task_handle) {
      error = "python_runtime_stop_requires_lifecycle_owner";
      return false;
    }
  }
  micropython_runtime_leave();
  const auto deadline = esp_timer_get_time() + 3000000;
  for (;;) {
    bool busy;
    { std::lock_guard<std::mutex> lock(g_state_mutex); busy = g_status.running || g_command_pending; }
    if (!busy) return micropython_shutdown_idle(runtime, error);
    if (esp_timer_get_time() >= deadline) { error = "python_script_stop_timeout"; return false; }
    vTaskDelay(1);
  }
}

void setup_micropython_runtime(lunaredu::lunaros::KernelRuntime &runtime) {
  lunaredu::micropython::StartAttempt attempt(g_setup_started);
  if (!attempt) {
    ESP_LOGW("micropython", "runtime setup ignored: already started");
    return;
  }
  g_owner_runtime = &runtime;
  g_idle_shutdown_requested.store(false, std::memory_order_release);
  struct StartupHeap {
    bool owner_started = false;
    ~StartupHeap() {
      if (!owner_started) {
        heap_caps_free(g_micropython_heap);
        g_micropython_heap = nullptr;
        g_engine_lease.release();
      }
    }
  } heap_attempt;

  if (!g_engine_lease.acquire(lunaredu::lunaros::apps::ExecutionEngine::MicroPython)) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_status.last_error = lunaredu::lunaros::apps::execution_engine_conflict();
    set_state_locked("error");
    return;
  }

  if (!ensure_vm_heap()) {
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_status.last_error = "runtime_heap_allocation_failed";
      set_state_locked("error");
    }
    return;
  }

  if (!ensure_log_storage()) {
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_status.last_error = "runtime_log_storage_allocation_failed";
      set_state_locked("error");
    }
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    const bool foreground_reserved = g_status.entered;
    g_status = MicroPythonRuntimeStatus();
    g_status.entered = foreground_reserved;
    g_log_write_index = 0;
    g_log_seq = 0;
    // Execution ids remain unique across on-demand VM destruction/recreation.
    g_active_execution_id.store(0, std::memory_order_release);
    g_cancel_through_execution_id.store(0, std::memory_order_release);
    g_repl_session_id = 0;
    g_command_pending = false;
    g_stdout_line_buffer.clear();
    for (size_t index = 0; index < kMicroPythonMaxLogs; ++index) {
      g_logs[index] = MicroPythonLogEntry();
    }
    set_state_locked("starting");
  }

  std::string executor_error;
  if (!g_executor_registered) {
    ServiceExecutorSnapshot executors[8]{};
    const size_t executor_count = runtime.executor_snapshot(
        executors, sizeof(executors) / sizeof(executors[0]));
    for (size_t index = 0; index < executor_count; ++index) {
      if (executors[index].id != nullptr &&
          std::strcmp(executors[index].id,
                      lunaredu::micropython::kRuntimeExecutorId) == 0) {
        if (executors[index].owner_service_id == nullptr ||
            std::strcmp(executors[index].owner_service_id,
                        "runtime.micropython") != 0 ||
            executors[index].queue_depth != kMicroPythonCommandQueueDepth) {
          executor_error = "python_service_executor_contract_mismatch";
        } else {
          g_executor_registered = true;
        }
        break;
      }
    }
  }
  if (!g_executor_registered && executor_error.empty() &&
      !runtime.setup_executors(
          {{lunaredu::micropython::kRuntimeExecutorId, "runtime.micropython",
            static_cast<uint8_t>(kMicroPythonCommandQueueDepth)}},
          &executor_error)) {
    // Standalone MicroPython products can still own an otherwise empty
    // registry. Full LunarOS products declare lunar.python during core boot.
  }
  if (!g_executor_registered && executor_error.empty()) {
    g_executor_registered = true;
  }
  if (!g_executor_registered) {
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_status.last_error =
          "runtime_service_executor_setup_failed:" + executor_error;
      set_state_locked("error");
      add_log_locked("error", g_status.last_error);
    }
    return;
  }

  const auto command_queue = xQueueCreate(
      static_cast<UBaseType_t>(kMicroPythonCommandQueueDepth),
      sizeof(MicroPythonVmCommand *));
  const auto ready = xSemaphoreCreateBinary();
  if (command_queue == nullptr || ready == nullptr) {
    if (command_queue != nullptr) vQueueDelete(command_queue);
    if (ready != nullptr) vSemaphoreDelete(ready);
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_status.last_error = "runtime_resource_allocation_failed";
      set_state_locked("error");
      add_log_locked("error", g_status.last_error);
    }
    return;
  }

  std::string task_error;
  const lunaredu::lunaros::ManagedTaskDefinition definition{
      .id = "micropython_vm",
      .owner_service_id = "runtime.micropython",
      .role = "language_vm",
      .stack_bytes = kMicroPythonTaskStackBytes,
      .priority = kMicroPythonTaskPriority,
      .core_affinity = lunaredu::lunaros::kManagedTaskDefaultCore,
      .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Feature,
      .memory = lunaredu::lunaros::ManagedTaskMemory::Internal,
      .allow_internal_fallback = false,
  };
  bool created = false;
  {
    // Publication and retirement use the same ownership lock. A successful
    // creation owns the handle here; its later retirement is not a failure.
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_vm_command_queue = command_queue;
    g_vm_ready = ready;
    created = lunaredu::lunaros::create_managed_task(
        runtime, definition, micropython_vm_task, &runtime, &g_vm_task_handle,
        task_error);
  }
  if (!created) {
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_vm_command_queue = nullptr;
      g_vm_ready = nullptr;
      g_status.last_error = "runtime_task_create_failed:" + task_error;
      set_state_locked("error");
      add_log_locked("error", g_status.last_error);
    }
    vQueueDelete(command_queue);
    vSemaphoreDelete(ready);
    return;
  }

  heap_attempt.owner_started = true;
  attempt.commit();
  wait_for_vm_startup(ready);

}

bool micropython_runtime_enter(std::string &error) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  error.clear();
  if(g_application_owned){error="application_owns_python_vm";return false;}
  if (g_idle_shutdown_requested || !g_status.initialized) {
    error = "runtime_not_initialized";
    set_rejected_error_locked(error);
    return false;
  }

  g_status.entered = true;
  g_status.last_error.clear();
  if (!g_status.running) {
    set_state_locked(g_status.repl_active ? "repl" : "idle");
  }
  add_log_locked("info", "runtime_enter");
  return true;
}

void micropython_runtime_leave() {
  uint32_t execution_id = 0;
  bool abort_active = false;
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_status.entered = false;
    g_status.repl_active = false;
    g_status.repl_mode.clear();
    ++g_repl_session_id;
    if (!g_application_owned)
      g_idle_shutdown_requested.store(true, std::memory_order_release);

    if (g_status.running) {
      execution_id = g_status.execution_id;
      request_cancel_through(execution_id);
      g_status.stop_requested = true;
      set_state_locked("stopping");
      abort_active = g_active_execution_id.load(std::memory_order_acquire) ==
                     execution_id;
      add_log_locked("info", "runtime_leave_requested");
    } else {
      g_status.current_program.clear();
      g_stdout_line_buffer.clear();
      set_state_locked("embedded_ready");
      add_log_locked("info", "runtime_leave");
    }
    if (g_vm_task_handle) lunaredu::lunaros::task_signal::notify(g_vm_task_handle);
  }

  if (abort_active) {
    request_micropython_vm_abort();
  }
}

bool micropython_run_string(const std::string &source, std::string &error) {
  return enqueue_source(source, "<string>", false, error);
}

bool micropython_repl_enter(std::string &error) {
  if (!micropython_runtime_enter(error)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_state_mutex);
  if (g_status.running) {
    error = "runtime_busy";
    set_rejected_error_locked(error);
    return false;
  }

  if (!g_status.repl_active) {
    ++g_repl_session_id;
  }
  g_status.repl_active = true;
  g_status.repl_mode = "controlled";
  g_status.current_program = "<repl>";
  g_status.last_error.clear();
  set_state_locked("repl");
  add_log_locked("info", "repl_enter");
  return true;
}

void micropython_repl_leave() {
  uint32_t execution_id = 0;
  bool abort_active = false;
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_status.repl_active = false;
    g_status.repl_mode.clear();
    ++g_repl_session_id;

    if (g_status.running) {
      execution_id = g_status.execution_id;
      request_cancel_through(execution_id);
      g_status.stop_requested = true;
      set_state_locked("stopping");
      abort_active = g_active_execution_id.load(std::memory_order_acquire) ==
                     execution_id;
      add_log_locked("info", "repl_leave_requested");
    } else {
      if (g_status.current_program == "<repl>") {
        g_status.current_program.clear();
      }
      g_stdout_line_buffer.clear();
      set_state_locked(g_status.entered ? "idle" : "embedded_ready");
      add_log_locked("info", "repl_leave");
    }
  }

  if (abort_active) {
    request_micropython_vm_abort();
  }
}

bool micropython_repl_feed(const std::string &input, std::string &error) {
  const std::string source = lunaredu::lunaros::text::trim_copy(input);
  return enqueue_source(source, "<repl>", true, error);
}

bool micropython_run_source(const std::string &source, const std::string &program, std::string &error) {
  return enqueue_source(source,program,false,error);
}

bool micropython_run_background(const std::string &source, const std::string &program,
                                uint32_t maximum_duration_ms, uint32_t &execution_id,
                                std::string &error) {
  execution_id = 0;
  if (source.empty() || source.size() > 24576 || program.empty() || program.size() > 96 ||
      maximum_duration_ms < 10 || maximum_duration_ms > 60000) {
    error = "invalid_background_program"; return false;
  }
  return enqueue_source(source, program, false, error, maximum_duration_ms, &execution_id);
}

bool micropython_stop_execution(uint32_t execution_id, std::string &error) {
  bool abort_active = false;
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (!execution_id || g_status.execution_id != execution_id) {
      error = "python_execution_changed"; return false;
    }
    if (!g_status.running) { error.clear(); return true; }
    request_cancel_through(execution_id);
    g_status.stop_requested = true;
    set_state_locked("stopping");
    abort_active = g_active_execution_id.load(std::memory_order_acquire) == execution_id;
  }
  if (abort_active) request_micropython_vm_abort();
  error.clear(); return true;
}

bool micropython_background_active() {
  return g_background_deadline_us.load(std::memory_order_acquire) != 0;
}
bool micropython_background_continue() {
  const auto deadline=g_background_deadline_us.load(std::memory_order_acquire);
  if(!deadline || command_cancelled(g_active_execution_id.load(std::memory_order_acquire)))return false;
  if(esp_timer_get_time()>=deadline){g_background_timed_out.store(true,std::memory_order_release);return false;}
  return true;
}

bool micropython_execution_receipt(uint32_t execution_id, MicroPythonExecutionReceipt &receipt) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  for (const auto &entry : g_execution_receipts) {
    if (execution_id && entry.execution_id == execution_id) { receipt = entry; return true; }
  }
  if (execution_id && g_status.execution_id == execution_id && g_status.running) {
    receipt = {execution_id, false, false, false, ""}; return true;
  }
  return false;
}

void micropython_stop(const char *reason) {
  uint32_t execution_id = 0;
  bool abort_active = false;
  const std::string stop_reason =
      reason != nullptr && reason[0] != '\0' ? reason : "manual";

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_status.repl_active = false;
    g_status.repl_mode.clear();
    ++g_repl_session_id;

    if (g_status.running) {
      execution_id = g_status.execution_id;
      request_cancel_through(execution_id);
      g_status.stop_requested = true;
      set_state_locked("stopping");
      abort_active = g_active_execution_id.load(std::memory_order_acquire) ==
                     execution_id;
      add_log_locked("info", "stop_requested:" + stop_reason);
    } else {
      g_status.current_program.clear();
      g_status.completed_at_ms = now_ms();
      g_status.stop_requested = false;
      set_state_locked(g_status.entered ? "idle" : "embedded_ready");
      add_log_locked("info", "stop_idle:" + stop_reason);
    }
  }

  if (abort_active) {
    request_micropython_vm_abort();
  }
}

MicroPythonRuntimeStatus micropython_runtime_status() {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  MicroPythonRuntimeStatus status = g_status;
  auto *device = lunaredu::micropython::runtime_device_context();
  status.application_id = device ? lunaredu::micropython::runtime_foreground_application(*device) : "";
  status.foreground = !status.application_id.empty();
  status.available = g_vm_initialized.load(std::memory_order_acquire) ||
      lunaredu::micropython::runtime_configured();
  if (status.available && !status.initialized && status.last_error.empty()) status.state = "vm_on_demand";
  return status;
}
MicroPythonRunFlags micropython_run_flags() {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  return {g_status.running,g_status.stop_requested};
}

void micropython_visit_logs(uint32_t after_seq, MicroPythonLogVisitor visitor,
                           void *context, uint32_t &next_seq) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  next_seq = g_log_seq;

  if (g_logs == nullptr || visitor == nullptr) {
    return;
  }

  for (size_t offset = 0; offset < kMicroPythonMaxLogs; ++offset) {
    const size_t index = (g_log_write_index + offset) % kMicroPythonMaxLogs;
    const MicroPythonLogEntry &entry = g_logs[index];
    if (entry.seq == 0 || entry.seq <= after_seq) {
      continue;
    }
    if (!visitor(entry, context)) break;
  }
}

void micropython_runtime_append_stdout(const char *str, size_t len) {
  if (str == nullptr || len == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_state_mutex);
  append_stdout_log_fragment_locked(std::string(str, len));
}
