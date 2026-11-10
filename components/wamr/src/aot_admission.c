#include "lunaros_wamr.h"
#include "../upstream/core/config.h"
#if WASM_ENABLE_AOT
#include "../upstream/core/iwasm/aot/aot_runtime.h"
#include <esp_heap_caps.h>
#include <esp_mmu_map.h>
#include <soc/mmu.h>
#include <string.h>
bool lunaros_wamr_aot_available(void) {
    void *data = heap_caps_malloc(64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) return false;
    void *code = (void *)((uintptr_t)data + SOC_IROM_LOW - SOC_DROM_LOW);
    esp_paddr_t d = 0, c = 0;
    mmu_target_t dt = 0, ct = 0;
    bool ok = esp_mmu_vaddr_to_paddr(data, &d, &dt) == ESP_OK
        && esp_mmu_vaddr_to_paddr(code, &c, &ct) == ESP_OK
        && d == c && dt == MMU_TARGET_PSRAM0 && ct == dt;
    heap_caps_free(data);
    return ok;
}
bool lunaros_wamr_aot_admit(wasm_module_t module) {
    AOTModule *m = (AOTModule *)module;
    if (!m || m->module_type != Wasm_Module_AoT || m->start_func_index != UINT32_MAX
        || m->import_memory_count || m->import_table_count || m->import_global_count
        || m->memory_count != 1 || m->table_count > 1) return false;
    const AOTMemoryType *mem = &m->memories[0];
    // WAMRC collapses fixed memories into a larger page when memory.grow is
    // absent. Enforce the byte budget, not the source-Wasm page encoding.
    if (mem->flags != 1 || !mem->init_page_count || mem->init_page_count > mem->max_page_count
        || !mem->num_bytes_per_page || mem->num_bytes_per_page > 262144
        || (uint64)mem->max_page_count * mem->num_bytes_per_page > 262144) return false;
    if (m->table_count && (m->tables[0].table_type.elem_type != VALUE_TYPE_FUNCREF
        || m->tables[0].table_type.flags != 1
        || m->tables[0].table_type.init_size > m->tables[0].table_type.max_size
        || m->tables[0].table_type.max_size > 64)) return false;
    // ABI names/signatures and the mandatory budget poll are checked by the
    // generic public-metadata admission before instantiation, for both engines.
    return true;
}
#else
bool lunaros_wamr_aot_available(void) { return false; }
bool lunaros_wamr_aot_admit(wasm_module_t module) { (void)module; return false; }
#endif
