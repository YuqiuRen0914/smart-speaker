#include "py/builtin.h"
#include "py/obj.h"
#include "py/runtime.h"

static mp_obj_t lunaredu_mp_open_stub(size_t n_args, const mp_obj_t *args,
                                      mp_map_t *kwargs) {
  (void)n_args;
  (void)args;
  (void)kwargs;
  mp_raise_NotImplementedError(MP_ERROR_TEXT("MicroPython file IO is not enabled"));
}

MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, lunaredu_mp_open_stub);
