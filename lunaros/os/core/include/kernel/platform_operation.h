#pragma once
#include "kernel/execution_admission.h"
namespace lunaredu::lunaros {
// One physical MCU may bind one device's admission ledger. Explicit binding
// keeps host runtimes independent. Detach is refused while reservations exist.
bool bind_platform_execution(ExecutionAdmission &, const void *owner);
bool unbind_platform_execution(ExecutionAdmission &, const void *owner);
class PlatformOperation {
public:
  PlatformOperation() = default;
  explicit PlatformOperation(uint32_t effects);
  ~PlatformOperation();
  PlatformOperation(const PlatformOperation &) = delete;
  PlatformOperation &operator=(const PlatformOperation &) = delete;
  bool begin(uint32_t effects);
  void end();
  bool accepted() const;
private:
  ExecutionAdmission *admission_ = nullptr;
  ExecutionLease lease_;
  bool accepted_ = false;
  uint32_t effects_ = 0;
};
} // namespace lunaredu::lunaros
