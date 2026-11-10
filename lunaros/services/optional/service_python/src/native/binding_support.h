#pragma once
#include <sdkconfig.h>
#include <cstdint>
#include "native/nlr_cleanup.h"
#include "lunaros_micropython_runtime_config.h"
#include "device_api/device_operation_types.h"
#include "device_api/device_api_context.h"
#include "micropython_runtime/micropython_runtime_context.h"
extern "C" {
#include "py/obj.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/qstr.h"
#include "py/runtime.h"
}
namespace lunaredu::micropython {
inline uint32_t py_rgb888_color(mp_obj_t object) {
  const mp_int_t color = mp_obj_get_int(object);
  if (color < 0 || color > 0xFFFFFF) {
    mp_raise_ValueError(MP_ERROR_TEXT("RGB888 color must be 0..16777215"));
  }
  return static_cast<uint32_t>(color);
}
inline mp_obj_t py_str(const char *value) {
  return mp_obj_new_str_from_cstr(value == nullptr ? "" : value);
}

inline mp_obj_t py_bool(bool value) { return mp_obj_new_bool(value); }

inline mp_obj_t py_u32(uint32_t value) { return mp_obj_new_int_from_uint(value); }

inline mp_obj_t py_i32(int32_t value) { return mp_obj_new_int(value); }

inline mp_obj_t py_u64(uint64_t value) { return mp_obj_new_int_from_ull(value); }

inline mp_obj_t py_float(float value) { return mp_obj_new_float(value); }

inline mp_obj_t qstr_obj(const char *name) {
  return MP_OBJ_NEW_QSTR(qstr_from_str(name));
}

inline void module_store(mp_obj_t module, const char *name, mp_obj_t value) {
  mp_obj_dict_t *globals = mp_obj_module_get_globals(module);
  mp_obj_dict_store(MP_OBJ_FROM_PTR(globals), qstr_obj(name), value);
}

inline void dict_store(mp_obj_t dict, const char *name, mp_obj_t value) {
  mp_obj_dict_store(dict, py_str(name), value);
}

inline bool capability_available(const char *id) {
  auto *device = runtime_device_context();
  return hardware_binding_access_allowed(id) && device != nullptr &&
         device->model.capabilities.active(id);
}

MP_NORETURN inline void raise_unavailable(const char *id) {
  mp_raise_msg_varg(&mp_type_RuntimeError,
                    MP_ERROR_TEXT("capability unavailable: %s"), id);
}

MP_NORETURN inline void raise_operation_failed(const char *operation) {
  mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s failed"),
                    operation);
}

inline void require_capability(const char *id) {
  if (!hardware_binding_access_allowed(id)) {
    mp_raise_msg_varg(&mp_type_RuntimeError,
        MP_ERROR_TEXT("resource managed by product; use lunaredu.apps.call: %s"), id);
  }
  if (!capability_available(id)) {
    raise_unavailable(id);
  }
}

inline mp_obj_t make_module(const char *name) {
  return mp_obj_new_module(qstr_from_str(name));
}

inline void register_submodule(mp_obj_t root, const char *attr, mp_obj_t submodule) {
  module_store(root, attr, submodule);
}


void register_app_binding_modules(mp_obj_t root);
void append_app_binding_status(mp_obj_t dict);
} // namespace lunaredu::micropython
