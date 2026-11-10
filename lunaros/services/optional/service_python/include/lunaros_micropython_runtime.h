#pragma once
#include "lunaros_micropython_service_id.h"

#include <stdint.h>
#include <string>

namespace lunaredu::lunaros { class KernelRuntime; }

// MicroPython 运行时状态。
struct MicroPythonRunFlags {
  bool running=false,stop_requested=false;
};
MicroPythonRunFlags micropython_run_flags();
struct MicroPythonRuntimeStatus {
  bool available = false;
  bool initialized = false;
  bool entered = false;
  bool running = false;
  bool stop_requested = false;
  bool repl_active = false;
  bool foreground = false;
  std::string application_id;
  std::string state;
  std::string last_error;
  std::string current_program;
  std::string repl_mode;
  uint32_t started_at_ms = 0;
  uint32_t completed_at_ms = 0;
  uint32_t log_seq = 0;
  uint32_t execution_id = 0;
};

// MicroPython 输出日志条目。
struct MicroPythonLogEntry {
  uint32_t seq = 0;
  uint32_t t_ms = 0;
  std::string level;
  std::string text;
  uint32_t execution_id = 0;
};

using MicroPythonLogVisitor = bool (*)(const MicroPythonLogEntry &, void *);
// Bounded ring read without initializing the VM. Entries are borrowed only
// during the callback under the log lock; callbacks must not reenter the VM.
// next_seq is the ring sequence at the same snapshot, including an empty ring.
void micropython_visit_logs(uint32_t after_seq, MicroPythonLogVisitor visitor,
                           void *context, uint32_t &next_seq);

// Initialize the VM and executor only; the App selects and submits user code.
void setup_micropython_runtime(lunaredu::lunaros::KernelRuntime &runtime);

struct MicroPythonStartupClaim {
  uint64_t revision = 0;
  explicit operator bool() const { return revision != 0; }
};

// Admission revision is shared with idle retirement under the VM state lock.
// A later preparation (including foreground preparation) supersedes an older
// failed request's claim, even before it enters the VM.
MicroPythonStartupClaim micropython_runtime_startup_claim();
// Reserve foreground use before asynchronous initialization. Background
// admission and retirement observe the same state lock and cannot steal it.
bool micropython_prepare_foreground(std::string &error);
// Attach a page to this exact background execution without admitting a second
// program or VM. The foreground reservation protects it from idle retirement.
bool micropython_attach_background_foreground(uint32_t execution_id,std::string &error);
bool micropython_retire_failed_startup(MicroPythonStartupClaim claim,
                                      std::string &error);

// 请求进入 MicroPython 运行时。
bool micropython_runtime_enter(std::string &error);

// 离开 MicroPython 运行时。
void micropython_runtime_leave();

// Cooperatively destroy an idle VM on its owner and await task release.
// Active scripts, REPL sessions and applications must be closed first.
bool micropython_shutdown_idle(lunaredu::lunaros::KernelRuntime &runtime,
                              std::string &error);

// Service lifecycle stop: cancel script/REPL work, wait for owner acknowledgement
// and reclaim the VM. A downloaded application retains its own lease and must
// finish closing first. Failure leaves the owner alive for a subsequent retry.
bool micropython_shutdown_runtime(lunaredu::lunaros::KernelRuntime &runtime,
                                 std::string &error);

// 异步提交一段 Python 源码字符串。
bool micropython_run_string(const std::string &source, std::string &error);

// Submit caller-owned source and an explicit diagnostic program name.
bool micropython_run_source(const std::string &source, const std::string &program, std::string &error);

// Background work never enters the Python foreground or takes over a REPL.
// The execution id is returned atomically with admission. The deadline is
// enforced by VM checkpoints; native ports must independently bound their I/O.
bool micropython_run_background(const std::string &source, const std::string &program,
                                uint32_t maximum_duration_ms, uint32_t &execution_id,
                                std::string &error);
// Cancel exactly the admitted execution, never a subsequent script/application.
bool micropython_stop_execution(uint32_t execution_id, std::string &error);

struct MicroPythonExecutionReceipt {
  uint32_t execution_id = 0;
  bool completed = false;
  bool succeeded = false;
  bool cancelled = false;
  std::string error;
};
// A bounded completion journal survives later admissions/rejected requests.
bool micropython_execution_receipt(uint32_t execution_id, MicroPythonExecutionReceipt &receipt);

// Request owner-side idle retirement after this exact background execution.
// Does not wait, cancel foreground/REPL work or retire a subsequent execution.
// Receipts and logs survive retirement. Pending returns false; true confirms
// owner cleanup and managed task release, rather than mere request acceptance.
bool micropython_retire_background_execution(uint32_t execution_id,
                                            std::string &error);

struct MicroPythonBackgroundPort {
  void *context = nullptr;
  bool (*call)(void *, const char *capability, const char *json,
               std::string &result, std::string &error) = nullptr;
};
// Set once during composition; callbacks execute on the VM owner, must have
// bounded I/O and check the active job's native authority on every call.
bool micropython_configure_background_port(MicroPythonBackgroundPort port);
bool micropython_background_active();
// Native wait ports cooperatively enforce the same deadline/cancellation as VM
// checkpoints. Does not consume the abort signal needed by the VM itself.
bool micropython_background_continue();
bool micropython_background_call(const char *capability, const char *json,
                                std::string &result, std::string &error);

// 进入交互式 REPL。
bool micropython_repl_enter(std::string &error);

// 离开交互式 REPL。
void micropython_repl_leave();

// 向 REPL 输入一段内容。
bool micropython_repl_feed(const std::string &input, std::string &error);

// 请求停止当前脚本；VM 在安全检查点退出，原生任务保持运行。
void micropython_stop(const char *reason = "manual");

// 获取当前运行时状态。
MicroPythonRuntimeStatus micropython_runtime_status();
