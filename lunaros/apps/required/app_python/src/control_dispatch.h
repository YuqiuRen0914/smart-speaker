#pragma once

#include <cstdint>
#include <string>

#include <ArduinoJson.h>

#include "lunaros_micropython_runtime.h"

namespace lunaredu::lunaros { class KernelRuntime; }

// Interpreter operations run on lunar.python; file/project administration runs
// on the App's main owner without starting a VM. Stop and snapshots never wait
// behind a running program. Device parsing stays in device_handlers.
bool dispatch_micropython_runtime_enter(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                        std::string &error);
bool dispatch_micropython_runtime_status(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                         std::string &error);
bool dispatch_micropython_runtime_leave(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                        std::string &error);
bool dispatch_micropython_repl_enter(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                     std::string &error);
bool dispatch_micropython_repl_leave(lunaredu::lunaros::KernelRuntime &runtime, MicroPythonRuntimeStatus *status,
                                     std::string &error);
bool dispatch_micropython_repl_feed(lunaredu::lunaros::KernelRuntime &runtime, JsonDocument &document,
                                    MicroPythonRuntimeStatus *status,
                                    std::string &error);
bool dispatch_micropython_run_script(lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
                                     MicroPythonRuntimeStatus *status,
                                     std::string &error);
// Project owner supplies the immutable record snapshot; never resolve /scripts.
bool dispatch_micropython_run_project_source(lunaredu::lunaros::KernelRuntime &,
    const std::string &source, const std::string &program,
    MicroPythonRuntimeStatus *, std::string &error);
bool dispatch_micropython_stop(lunaredu::lunaros::KernelRuntime &runtime, const std::string &reason,
                               MicroPythonRuntimeStatus *status,
                               std::string &error);
bool dispatch_micropython_delete_script(lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
                                        std::string &error);
bool dispatch_micropython_read_script(lunaredu::lunaros::KernelRuntime &runtime,
                                      const std::string &name,
                                      std::string &content,
                                      std::string &error);
bool dispatch_micropython_set_autorun(lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
                                      std::string &error);
bool dispatch_micropython_fill_logs_payload(lunaredu::lunaros::KernelRuntime &runtime, JsonDocument &response,
                                            uint32_t after_seq,
                                            std::string &error);
bool dispatch_micropython_fill_script_list_payload(lunaredu::lunaros::KernelRuntime &runtime, JsonDocument &response,
                                                   std::string &error);
bool dispatch_micropython_upload_script(lunaredu::lunaros::KernelRuntime &runtime, const std::string &name,
                                        JsonDocument &document,
                                        bool set_autorun,
                                        bool run_after_upload,
                                        MicroPythonRuntimeStatus *status,
                                        std::string &error);
