#include "micropython_execution.h"
#include "lunaros_micropython_machine.h"

#include <atomic>

#include "sdkconfig.h"

#ifndef CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS
#define CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS 0
#endif

#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS
#include "lunaros_micropython_runtime_config.h"
#include "micropython_runtime_context.h"
#endif

extern "C" {
#include "py/compile.h"
#include "py/lexer.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
}

namespace {

std::atomic<bool> g_vm_abort_requested{false};

void stop_script_owned_motion() {
  lunaredu::micropython::release_micropython_machine_outputs();
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS
  std::string response;
  std::string error;
  auto *capabilities = lunaredu::micropython::runtime_application_capabilities();
  const auto *port = lunaredu::micropython::runtime_robot_motion_port();
  if (capabilities != nullptr && port != nullptr && port->execute != nullptr) {
    (void)port->execute(*capabilities, "{}", "stop", response, error);
  }
#endif
}

} // namespace

extern "C" int lunaros_micropython_take_vm_abort_request() {
  return g_vm_abort_requested.exchange(false, std::memory_order_acq_rel) ? 1
                                                                         : 0;
}

MicroPythonExecutionOutcome
execute_micropython_source_on_owner(const std::string &source) {
  nlr_buf_t nlr;
  nlr_set_abort(&nlr);

  if (nlr_push(&nlr) == 0) {
    mp_lexer_t *lexer = mp_lexer_new_from_str_len(
        MP_QSTR__lt_stdin_gt_, source.c_str(), source.length(), 0);
    const qstr source_name = lexer->source_name;
    mp_parse_tree_t parse_tree = mp_parse(lexer, MP_PARSE_FILE_INPUT);
    mp_obj_t module_function = mp_compile(&parse_tree, source_name, true);
    mp_call_function_0(module_function);
    nlr_pop();
    nlr_set_abort(nullptr);
    stop_script_owned_motion();
    return {MicroPythonExecutionState::Completed, ""};
  }

  nlr_set_abort(nullptr);
  stop_script_owned_motion();
  if (nlr.ret_val == nullptr) {
    return {MicroPythonExecutionState::Aborted, "script_stopped"};
  }

  const mp_obj_t exception = (mp_obj_t)nlr.ret_val;
  const char *type_name = mp_obj_get_type_str(exception);
  mp_obj_print_exception(&mp_plat_print, exception);
  return {MicroPythonExecutionState::Failed,
          type_name != nullptr && type_name[0] != '\0'
              ? std::string("python_exception:") + type_name
              : std::string("python_exception")};
}

void request_micropython_vm_abort() {
  // MicroPython VM state belongs to the owner task. The VM hook consumes this
  // atomic flag on that task instead of mutating MP_STATE_VM cross-core.
  g_vm_abort_requested.store(true, std::memory_order_release);
}

void clear_micropython_vm_abort_request() {
  g_vm_abort_requested.store(false, std::memory_order_release);
}
