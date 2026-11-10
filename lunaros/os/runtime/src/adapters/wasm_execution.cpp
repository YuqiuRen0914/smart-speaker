#include "adapters/execution_adapters.h"
#ifdef LUNAROS_WASM_SERVICE
#include "lunaros_wasm_app_backend.h"
namespace lunaredu::lunaros {
constinit const RuntimeExecutionContribution kWasmExecutionContribution{
    .id="wasm",.configure_backend=apps::register_wasm_application_backends};
}
#endif
