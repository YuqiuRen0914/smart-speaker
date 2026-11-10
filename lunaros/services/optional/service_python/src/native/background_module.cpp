#include "lunaros_micropython_runtime.h"
#include "lunaros_micropython_native_modules.h"
#include "native/binding_support.h"
#include <cstring>

namespace {
MicroPythonBackgroundPort background_port;
bool configured = false;
mp_obj_t call(mp_obj_t capability_obj, mp_obj_t arguments_obj) {
  const char *capability = mp_obj_str_get_str(capability_obj);
  size_t bytes = 0;
  const char *arguments = mp_obj_str_get_data(arguments_obj, &bytes);
  if (!*capability || std::strlen(capability) > 96 || bytes > 2048 || std::memchr(arguments, 0, bytes))
    mp_raise_ValueError(MP_ERROR_TEXT("invalid lunar.call arguments"));
  std::string input(arguments, bytes), result, error;
  lunaredu::micropython::NlrCleanup input_cleanup(input), result_cleanup(result), error_cleanup(error);
  if (!micropython_background_call(capability, input.c_str(), result, error))
    lunaredu::micropython::raise_operation_failed(error.c_str());
  if (result.size() > 4096) mp_raise_ValueError(MP_ERROR_TEXT("lunar.call result too large"));
  return mp_obj_new_str(result.data(), result.size());
}
MP_DEFINE_CONST_FUN_OBJ_2(call_obj, call);
void setup() {
  mp_obj_t module = mp_obj_new_module(qstr_from_str("lunar"));
  lunaredu::micropython::module_store(module, "call", MP_OBJ_FROM_PTR(&call_obj));
}
}
bool micropython_configure_background_port(MicroPythonBackgroundPort port) {
  if (configured || !port.call || micropython_runtime_status().initialized) return false;
  if (!register_micropython_native_module({"lunar", setup})) return false;
  background_port = port; configured = true; return true;
}
bool micropython_background_call(const char *capability, const char *json,
                                std::string &result, std::string &error) {
  if (!micropython_background_active() || !configured || !background_port.call) {
    error = "background_program_required"; return false;
  }
  return background_port.call(background_port.context, capability, json, result, error);
}
