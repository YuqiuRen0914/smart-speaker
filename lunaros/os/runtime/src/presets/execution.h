#pragma once
#include "runtime/execution_contribution.h"
#include "adapters/execution_adapters.h"
#include <array>
#include <span>
namespace lunaredu::lunaros {
// A compile-time assembly table: no local-static guards or writable pointer
// arrays. Concrete names stay here, outside generic startup and graph code.
inline constexpr auto kDefaultExecutionContributions=std::to_array<const RuntimeExecutionContribution *>({
#ifdef LUNAROS_WASM_SERVICE
  &kWasmExecutionContribution,
#endif
#ifdef LUNAROS_PYTHON_SERVICE
  &kPythonExecutionContribution,
#endif
  nullptr});
constexpr std::span<const RuntimeExecutionContribution *const> default_execution_contributions() {
  return {kDefaultExecutionContributions.data(),kDefaultExecutionContributions.size()-1};
}
}
