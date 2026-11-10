#pragma once

#include <string>

enum class MicroPythonExecutionState {
  Completed,
  Aborted,
  Failed,
};

struct MicroPythonExecutionOutcome {
  MicroPythonExecutionState state = MicroPythonExecutionState::Failed;
  std::string error;
};

// Compile and execute source on the MicroPython owner task.
MicroPythonExecutionOutcome
execute_micropython_source_on_owner(const std::string &source);

// Request an asynchronous abort. This is safe to call from another task while
// the VM is executing Python bytecode.
void request_micropython_vm_abort();

// Clear a request that belongs to a completed or rejected execution. Call this
// only while the owner task is between VM executions.
void clear_micropython_vm_abort_request();
