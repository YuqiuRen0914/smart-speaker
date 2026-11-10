/* Keep the pinned fork within the interpreter profile reviewed by LunarOS.
 * These checks use the same definitions as the compiled runtime, rather than
 * assuming upstream CMake defaults. Changes require a compatibility review.
 */
#include "../upstream/core/config.h"

#if !WASM_ENABLE_INTERP || !WASM_ENABLE_CALL_INDIRECT_OVERLONG \
    || !WASM_ENABLE_INSTRUCTION_METERING
#error "LunarOS WAMR requires the metered interpreter and padded call_indirect support"
#endif

#if WASM_ENABLE_FAST_INTERP || WASM_ENABLE_JIT \
    || WASM_ENABLE_FAST_JIT || WASM_ENABLE_LIBC_WASI \
    || WASM_ENABLE_GC || WASM_ENABLE_STRINGREF || WASM_ENABLE_REF_TYPES \
    || WASM_ENABLE_EXTENDED_CONST_EXPR || WASM_ENABLE_SIMD \
    || WASM_ENABLE_SHARED_MEMORY || WASM_ENABLE_MULTI_MODULE
#error "LunarOS WAMR feature profile changed; review the pinned fork before enabling it"
#endif

#if WASM_ENABLE_AOT && (!WASM_MEM_DUAL_BUS_MIRROR || !WASM_DISABLE_HW_BOUND_CHECK \
    || !WASM_DISABLE_STACK_HW_BOUND_CHECK || WASM_ENABLE_AOT_STACK_FRAME)
#error "LunarOS AOT requires PSRAM mirror and software bounds/stack checks"
#endif
