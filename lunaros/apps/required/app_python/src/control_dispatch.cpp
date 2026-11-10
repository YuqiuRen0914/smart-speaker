#include "control_dispatch.h"
#include "script_catalog.h"
#include "project_records.h"

#include "device_handlers.h"

#include "lunaros_micropython_runtime_config.h"
#include "kernel/service_executor.h"
#include "kernel/runtime.h"


#include "lunaros_python_application.h"
#include "device_api/device_api_context.h"
#include "app/execution/application_context.h"

namespace {

enum class MicroPythonOperation : uint8_t {
  RuntimeEnter,
  RuntimeStatus,
  RuntimeLeave,
  ReplEnter,
  ReplLeave,
  ReplFeed,
  RunScript,
  RunProjectSource,
  DeleteScript,
  ReadScript,
  SetAutorun,
  FillScriptListPayload,
  UploadScript,
};

struct MicroPythonOperationRequest {
  MicroPythonOperation operation = MicroPythonOperation::RuntimeStatus;
  std::string text;
  std::string source, program;
  // A project record snapshot lives through synchronous main -> VM admission.
  // The VM command takes its own copy before invoke() returns.
  const std::string *project_source = nullptr;
  std::string *text_output = nullptr;
  JsonDocument *document = nullptr;
  MicroPythonRuntimeStatus *status = nullptr;
  uint32_t after_sequence = 0;
  bool set_autorun = false;
  bool run_after_upload = false;
};

void store_status(MicroPythonOperationRequest &request) {
  if (request.status != nullptr) {
    *request.status = micropython_runtime_status();
  }
}

bool require_document(MicroPythonOperationRequest &request,
                      lunaredu::lunaros::ServiceOperationResult &result,
                      const char *error) {
  if (request.document != nullptr) {
    return true;
  }
  result.ok = false;
  result.error = error;
  return false;
}

void execute_micropython_operation(
    void *context,
    lunaredu::lunaros::ServiceOperationResult &result) {
  auto &request = *static_cast<MicroPythonOperationRequest *>(context);
  std::string error;
  switch (request.operation) {
  case MicroPythonOperation::RuntimeEnter:
    result.ok = micropython_runtime_enter(result.error);
    store_status(request);
    return;
  case MicroPythonOperation::RuntimeStatus:
    store_status(request);
    return;
  case MicroPythonOperation::RuntimeLeave:
    micropython_runtime_leave();
    store_status(request);
    return;
  case MicroPythonOperation::ReplEnter:
    result.ok = micropython_repl_enter(result.error);
    store_status(request);
    return;
  case MicroPythonOperation::ReplLeave:
    micropython_repl_leave();
    store_status(request);
    return;
  case MicroPythonOperation::ReplFeed:
    if (!require_document(request, result, "missing_request_document")) {
      return;
    }
    result.ok = micropython_repl_feed(
        std::string(
            (*request.document)["input"] | ""),
        result.error);
    store_status(request);
    return;
  case MicroPythonOperation::RunScript:
  case MicroPythonOperation::RunProjectSource:
    result.ok = micropython_runtime_enter(result.error) &&
                micropython_run_source(request.project_source?*request.project_source:request.source,
                                       request.program,result.error);
    store_status(request);
    return;
  case MicroPythonOperation::DeleteScript:
    result.ok = lunaredu::python::scripts::remove(request.text, result.error);
    return;
  case MicroPythonOperation::ReadScript:
    if (request.text_output == nullptr) {
      result.ok = false;
      result.error = "missing_script_content_output";
      return;
    }
    result.ok = lunaredu::python::scripts::read(request.text, *request.text_output,
                                        result.error);
    return;
  case MicroPythonOperation::SetAutorun:
    result.ok = lunaredu::python::scripts::set_autorun(request.text, result.error);
    return;
  case MicroPythonOperation::FillScriptListPayload:
    if (!require_document(request, result, "missing_response_document")) {
      return;
    }
    result.ok = lunaredu::python::scripts::list(
        (*request.document)["payload"].to<JsonObject>(), result.error);
    return;
  case MicroPythonOperation::UploadScript:
    if (!require_document(request, result, "missing_request_document")) {
      return;
    }
    result.ok = lunaredu::python::scripts::write(
        request.text,
        std::string(
            (*request.document)["content"] | ""),
        request.set_autorun, result.error);
    store_status(request);
    return;
  }
  result.ok = false;
  result.error = "unsupported_micropython_operation";
}

bool invoke_python(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonOperationRequest &request, std::string &error) {
  if (!lunaredu::micropython::ensure_runtime_started(error)) {
    return false;
  }
  return runtime.invoke_executor(
      lunaredu::micropython::kRuntimeExecutorId,
      execute_micropython_operation, &request, error);
}

struct ForegroundRequest {
  lunaredu::lunaros::KernelRuntime &runtime;
  MicroPythonOperationRequest &request;
};
bool invoke(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonOperationRequest &request, std::string &error) {
  error.clear();
  if (!lunaredu::micropython::core_enabled()) { error = "micropython_explicitly_disabled"; return false; }
  if (!lunaredu::micropython::runtime_configured()) { error = "runtime_service_not_configured"; return false; }
  const bool file_operation = request.operation == MicroPythonOperation::DeleteScript ||
      request.operation == MicroPythonOperation::ReadScript ||
      request.operation == MicroPythonOperation::SetAutorun ||
      request.operation == MicroPythonOperation::FillScriptListPayload ||
      (request.operation == MicroPythonOperation::UploadScript && !request.run_after_upload);
  if (file_operation) {
    // File/project administration belongs to the App's main owner. It never
    // acquires the interpreter or waits behind a running Python program.
    return runtime.invoke_executor(lunaredu::lunaros::kMainServiceExecutor,
        execute_micropython_operation, &request, error);
  }
  const bool needs_foreground = request.operation == MicroPythonOperation::RunScript ||
      request.operation == MicroPythonOperation::RunProjectSource ||
      request.operation == MicroPythonOperation::ReplEnter ||
      request.operation == MicroPythonOperation::ReplFeed ||
      (request.operation == MicroPythonOperation::UploadScript && request.run_after_upload);
  if (!needs_foreground) return invoke_python(runtime, request, error);
  ForegroundRequest command{runtime, request};
  // The main owner acquires the app before calling Python; the VM never calls
  // back into a main owner that might already be waiting for it (including USB).
  return runtime.invoke_executor(lunaredu::lunaros::kMainServiceExecutor,
      [](void *p, lunaredu::lunaros::ServiceOperationResult &result) {
        auto &command = *static_cast<ForegroundRequest *>(p);
        auto *device = lunaredu::micropython::runtime_device_context();
        if (!device) { result.ok = false; result.error = "python_device_context_unavailable"; return; }
        auto &request=command.request;
        if (request.operation == MicroPythonOperation::UploadScript) {
          if (!request.document) { result.ok=false; result.error="missing_request_document"; return; }
          if (!lunaredu::python::scripts::write(request.text,std::string((*request.document)["content"] | ""),request.set_autorun,result.error)) {
            result.ok=false; return;
          }
          request.operation=MicroPythonOperation::RunScript;
        }
        if (request.operation == MicroPythonOperation::RunScript &&
            !lunaredu::python::scripts::load_source(request.text,request.source,request.program,result.error)) {
          result.ok=false; return;
        }
        if (!lunaredu::python::SystemApplication::enter(*device, result.error)) { result.ok = false; return; }
        result.ok = invoke_python(command.runtime, command.request, result.error);
        if (result.ok) lunaredu::python::SystemApplication::accepted(*device,
            command.request.operation == MicroPythonOperation::ReplEnter || command.request.operation == MicroPythonOperation::ReplFeed);
        else lunaredu::python::SystemApplication::rejected(*device);
      }, &command, error);
}

} // namespace

namespace lunaredu::python {
bool run_boot_script(lunaros::ApplicationContext &device, std::string &error) {
  std::string name;
  if (!scripts::autorun(name,error)) return false;
  if (name.empty()) return true;
  std::string source,program;bool project=false;
  if(!project_boot_source(name,source,program,project,error))return false;
  if(project)return dispatch_micropython_run_project_source(device.runtime,source,program,nullptr,error);
  return dispatch_micropython_run_script(device.runtime,name,nullptr,error);
}
}

bool dispatch_micropython_runtime_enter(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                        std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::RuntimeEnter;
  request.status = status;
  return invoke(runtime, request, error);
}

bool dispatch_micropython_runtime_status(lunaredu::lunaros::KernelRuntime &, MicroPythonRuntimeStatus *status,
                                         std::string &error) {
  error.clear();
  if (status == nullptr) {
    error = "missing_runtime_status_output";
    return false;
  }
  // Status polling must not defeat on-demand startup. The runtime protects its
  // snapshot internally, so a direct read is safe before or after VM startup.
  *status = micropython_runtime_status();
  return true;
}

bool dispatch_micropython_runtime_leave(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                        std::string &error) {
  (void)runtime;
  error.clear();
  if (!lunaredu::micropython::core_enabled()) { error = "micropython_explicitly_disabled"; return false; }
  micropython_runtime_leave();
  if (status) *status = micropython_runtime_status();
  return true;
}

bool dispatch_micropython_repl_enter(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                     std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::ReplEnter;
  request.status = status;
  return invoke(runtime, request, error);
}

bool dispatch_micropython_repl_leave(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                     std::string &error) {
  (void)runtime;
  error.clear();
  if (!lunaredu::micropython::core_enabled()) { error = "micropython_explicitly_disabled"; return false; }
  micropython_repl_leave();
  if (status) *status = micropython_runtime_status();
  return true;
}

bool dispatch_micropython_repl_feed(lunaredu::lunaros::KernelRuntime &runtime, JsonDocument &document,
                                    MicroPythonRuntimeStatus *status,
                                    std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::ReplFeed;
  request.document = &document;
  request.status = status;
  return invoke(runtime, request, error);
}

bool dispatch_micropython_run_script(lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
                                     MicroPythonRuntimeStatus *status,
                                     std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::RunScript;
  request.text = name;
  request.status = status;
  return invoke(runtime, request, error);
}

bool dispatch_micropython_run_project_source(lunaredu::lunaros::KernelRuntime &runtime,
    const std::string &source,const std::string &program,
    MicroPythonRuntimeStatus *status,std::string &error) {
  if(source.empty()){error="project_content_empty";return false;}
  MicroPythonOperationRequest request;
  request.operation=MicroPythonOperation::RunProjectSource;
  request.project_source=&source;request.program=program;request.status=status;
  return invoke(runtime,request,error);
}

bool dispatch_micropython_stop(lunaredu::lunaros::KernelRuntime &, const std::string &reason,
                               MicroPythonRuntimeStatus *status,
                               std::string &error) {
  // Stop only publishes a cancellation flag. Queueing it behind the running
  // program prevents an infinite loop from ever receiving the cancellation.
  error.clear();
  if (!lunaredu::micropython::core_enabled()) {
    error = "micropython_explicitly_disabled"; return false;
  }
  micropython_stop(reason.c_str());
  if (status != nullptr) *status = micropython_runtime_status();
  return true;
}

bool dispatch_micropython_delete_script(lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
                                        std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::DeleteScript;
  request.text = name;
  return invoke(runtime, request, error);
}

bool dispatch_micropython_read_script(
    lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
    std::string &content, std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::ReadScript;
  request.text = name;
  request.text_output = &content;
  return invoke(runtime, request, error);
}

bool dispatch_micropython_set_autorun(lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
                                      std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::SetAutorun;
  request.text = name;
  return invoke(runtime, request, error);
}

bool dispatch_micropython_fill_logs_payload(lunaredu::lunaros::KernelRuntime &, JsonDocument &response,
                                            uint32_t after_seq,
                                            std::string &error) {
  // The log ring owns its lock; snapshots must stay readable while the VM is
  // executing and must not allocate a VM merely because a panel polls output.
  error.clear();
  if (!lunaredu::micropython::core_enabled()) {
    error = "micropython_explicitly_disabled"; return false;
  }
  auto payload = response["payload"].to<JsonObject>();
  auto logs = payload["logs"].to<JsonArray>();
  uint32_t next_seq = 0;
  micropython_visit_logs(after_seq,
      [](const MicroPythonLogEntry &entry, void *context) {
        auto &logs = *static_cast<JsonArray *>(context);
        auto item = logs.add<JsonObject>();
        item["seq"] = entry.seq; item["t_ms"] = entry.t_ms;
        item["level"] = entry.level; item["text"] = entry.text;
        return true;
      }, &logs, next_seq);
  payload["ok"] = true; payload["next_seq"] = next_seq;
  return true;
}

bool dispatch_micropython_fill_script_list_payload(lunaredu::lunaros::KernelRuntime &runtime, JsonDocument &response,
                                                   std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::FillScriptListPayload;
  request.document = &response;
  return invoke(runtime, request, error);
}

bool dispatch_micropython_upload_script(lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
                                        JsonDocument &document,
                                        bool set_autorun,
                                        bool run_after_upload,
                                        MicroPythonRuntimeStatus *status,
                                        std::string &error) {
  MicroPythonOperationRequest request;
  request.operation = MicroPythonOperation::UploadScript;
  request.text = name;
  request.document = &document;
  request.status = status;
  request.set_autorun = set_autorun;
  request.run_after_upload = run_after_upload;
  return invoke(runtime, request, error);
}
