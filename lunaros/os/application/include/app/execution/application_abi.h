#ifndef LUNAROS_APPLICATION_ABI_H
#define LUNAROS_APPLICATION_ABI_H
/* Canonical App ABI 1. This C-compatible declaration also feeds the SDK.
 * X(name, host method, WAMR signature, i32 parameters, device permission,
 *   guest parameters). All functions return one i32. */
#define LUNAROS_APP_ABI_MODULE "lunaros_app_v1"
#define LUNAROS_APP_ABI_IMPORTS(X) \
  X(graphics_submit, submit, "(ii)i", 2, 0, (const void *, int)) \
  X(storage_read, read, "(ii)i", 2, 0, (void *, int)) \
  X(storage_write, write, "(ii)i", 2, 0, (const void *, int)) \
  X(log_write, log, "(ii)i", 2, 0, (const char *, int)) \
  X(device_call, device, "(iiii)i", 4, 1, (int, int, int, int))
/* X(name, Wasm external kind (0=function, 2=memory), i32 parameters,
 *   required export). Optional callbacks remain optional for old packages. */
#define LUNAROS_APP_ABI_EXPORTS(X) \
  X(memory, 2, 0, 0) \
  X(luna_init, 0, 3, 1) \
  X(luna_event, 0, 4, 1) \
  X(luna_stop, 0, 0, 1) \
  X(luna_state, 0, 0, 0) \
  X(luna_save, 0, 0, 0) \
  X(luna_validate_save, 0, 0, 0)
#endif
