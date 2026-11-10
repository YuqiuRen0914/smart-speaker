// Include common MicroPython embed configuration.
#include <port/mpconfigport_common.h>
#include "micropython_engine_hooks.h"

// Start from the smallest supported ROM feature set and add features
// incrementally as the runtime integration matures.
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

// Minimal execution support for the first embed milestone.
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_ENABLE_VM_ABORT                 (1)
#define MICROPY_VM_HOOK_INIT uint32_t micropython_engine_branch_count = 0;
#define MICROPY_VM_HOOK_LOOP \
    do { \
        if (micropython_engine_vm_checkpoint(&micropython_engine_branch_count)) { \
            mp_sched_vm_abort(); \
        } \
    } while (0);
#define MICROPY_GCREGS_SETJMP                   (1)
#define MICROPY_NLR_SETJMP                      (1)
#define MICROPY_PY_GC                           (1)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_FLOAT)
// MicroPython's long-long backend requires a 32-bit machine word.
// Use the portable MPZ backend for native 64-bit acceptance tests.
#if UINTPTR_MAX > UINT32_MAX
#define MICROPY_LONGINT_IMPL                    (MICROPY_LONGINT_IMPL_MPZ)
#else
#define MICROPY_LONGINT_IMPL                    (MICROPY_LONGINT_IMPL_LONGLONG)
#endif
#define MICROPY_PY_SYS                          (1)
#define MICROPY_PY_IO                           (1)
#define MICROPY_PY_OS                           (0)
#define MICROPY_PY_MATH                         (1)
#define MICROPY_PY_MATH_CONSTANTS               (1)
#define MICROPY_PY_COLLECTIONS                  (1)
#define MICROPY_PY_ARRAY                        (1)
#define MICROPY_PY_STRUCT                       (1)

#define MICROPY_PY_SYS_PLATFORM                 "lunaredu-esp32s3"
#define MICROPY_HW_BOARD_NAME                   "LunarEdu ESP32-S3"
#define MICROPY_HW_MCU_NAME                     "ESP32-S3"
