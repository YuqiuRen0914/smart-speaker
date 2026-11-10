#pragma once
#include <stdbool.h>
#include "wasm_export.h"
#ifdef __cplusplus
extern "C" {
#endif
bool lunaros_wamr_aot_available(void);
// AOT structural/resource gate only. Pair with module_admit before instantiation;
// it intentionally carries no application ABI policy.
bool lunaros_wamr_aot_admit(wasm_module_t module);
// Generic caller-owned ABI policy, inspected with upstream public metadata APIs.
// The engine adapter has no dependency on Application or its import names.
typedef struct {const char *name;uint32_t parameters;bool device;} LunarWamrImport;
typedef struct {const char *name;uint32_t kind,parameters;bool required;} LunarWamrExport;
typedef struct {
  const char *module;
  const LunarWamrImport *imports;uint32_t import_count;
  const LunarWamrExport *exports;uint32_t export_count;
} LunarWamrContract;
bool lunaros_wamr_module_admit(wasm_module_t,const LunarWamrContract *,bool aot,bool *uses_device);
#ifdef __cplusplus
}
#endif
