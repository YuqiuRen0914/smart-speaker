#pragma once
// qstr.h transitively reads misc.h, whose varargs declarations require
// va_start to be available before the forced configuration include.
#include <stdarg.h>
#include <stddef.h>
// Read-only lesson projects share native Python imports with the browser VM.
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#define MICROPY_HAS_FILE_READER (1)
#define MICROPY_PY_SYS_PATH (1)
#define MICROPY_PY_BUILTINS_BYTEARRAY (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)
#define MICROPY_PY_BUILTINS_MIN_MAX (1)
// The locked minimal-ROM generated QSTR table omits this optional message.
// Intern it at initialization, before stack exhaustion can occur, without
// rewriting generated vendor files or allocating while raising the exception.
// qstr is size_t in the pinned engine. Avoid importing VM headers before each
// upstream translation unit establishes its standard-header configuration.
extern size_t lunaros_mp_recursion_qstr;
#define MP_QSTR_maximum_space_recursion_space_depth_space_exceeded lunaros_mp_recursion_qstr
