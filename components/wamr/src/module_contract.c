#include "lunaros_wamr.h"
#include <string.h>
static bool signature(wasm_func_type_t type,uint32_t count) {
    if (!type || wasm_func_type_get_param_count(type)!=count
        || wasm_func_type_get_result_count(type)!=1
        || wasm_func_type_get_result_valkind(type,0)!=WASM_I32) return false;
    for(uint32_t i=0;i<count;++i)
        if(wasm_func_type_get_param_valkind(type,i)!=WASM_I32)return false;
    return true;
}
bool lunaros_wamr_module_admit(wasm_module_t module,const LunarWamrContract *contract,bool aot,bool *uses_device) {
    if(uses_device)*uses_device=false;
    if(!module||!contract||!contract->module||!contract->imports||!contract->exports
        ||contract->import_count>31||contract->export_count>31)return false;
    const int32_t imports=wasm_runtime_get_import_count(module),exports=wasm_runtime_get_export_count(module);
    if(imports<0||exports<0||(uint32_t)imports>contract->import_count+(aot?1u:0u)
        ||(uint32_t)exports>contract->export_count)return false;
    uint32_t seen_imports=0,seen_exports=0;bool poll=false;
    for(int32_t i=0;i<imports;++i){
        wasm_import_t entry={0};wasm_runtime_get_import_type(module,i,&entry);
        if(!entry.module_name||!entry.name||entry.kind!=WASM_IMPORT_EXPORT_KIND_FUNC||!entry.linked)return false;
        if(aot&&!strcmp(entry.module_name,"__lunaros_aot_v1")&&!strcmp(entry.name,"poll")){
            if(poll||!signature(entry.u.func_type,0))return false;
            poll=true;continue;
        }
        if(strcmp(entry.module_name,contract->module))return false;
        uint32_t n=0;while(n<contract->import_count&&strcmp(entry.name,contract->imports[n].name))++n;
        if(n==contract->import_count||(seen_imports&(1u<<n))||!signature(entry.u.func_type,contract->imports[n].parameters))return false;
        seen_imports|=1u<<n;
        if(uses_device&&contract->imports[n].device)*uses_device=true;
    }
    for(int32_t i=0;i<exports;++i){
        wasm_export_t entry={0};wasm_runtime_get_export_type(module,i,&entry);
        if(!entry.name)return false;
        uint32_t n=0;while(n<contract->export_count&&strcmp(entry.name,contract->exports[n].name))++n;
        if(n==contract->export_count||(seen_exports&(1u<<n))||(uint32_t)entry.kind!=contract->exports[n].kind)return false;
        if(entry.kind==WASM_IMPORT_EXPORT_KIND_FUNC&&!signature(entry.u.func_type,contract->exports[n].parameters))return false;
        seen_exports|=1u<<n;
    }
    for(uint32_t n=0;n<contract->export_count;++n)
        if(contract->exports[n].required&&!(seen_exports&(1u<<n)))return false;
    return !aot||poll;
}
