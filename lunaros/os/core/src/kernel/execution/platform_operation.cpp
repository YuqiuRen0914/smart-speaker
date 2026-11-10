#include "kernel/platform_operation.h"
namespace lunaredu::lunaros {
namespace {
StaticMutex binding_mutex;
ExecutionAdmission *bound_admission = nullptr;
const void *bound_owner = nullptr;
constexpr char kOperationOwner[] = "platform.operation";
}
bool bind_platform_execution(ExecutionAdmission &admission, const void *owner) {
  if (!owner) return false;
  StaticLockGuard lock(binding_mutex);
  if (bound_admission && (bound_admission!=&admission || bound_owner!=owner)) return false;
  bound_admission=&admission;bound_owner=owner;return true;
}
bool unbind_platform_execution(ExecutionAdmission &admission, const void *owner) {
  StaticLockGuard lock(binding_mutex);
  if (bound_admission!=&admission || bound_owner!=owner || !admission.can_reset()) return false;
  bound_admission=nullptr;bound_owner=nullptr;return true;
}
PlatformOperation::PlatformOperation(uint32_t effects) { (void)begin(effects); }
PlatformOperation::~PlatformOperation() { end(); }
bool PlatformOperation::begin(uint32_t effects) {
  if (accepted_) return (effects & ~effects_)==0;
  StaticLockGuard lock(binding_mutex);
  if (!bound_admission) { accepted_=true;effects_=effects;return true; }
  ExecutionContract contract;
  contract.kind=ExecutionClass::Background;contract.bounded=true;
  contract.effects=effects;contract.core=0;
  const char *error=nullptr;
  if (!bound_admission->reserve(kOperationOwner,contract,lease_,error)) return false;
  admission_=bound_admission;accepted_=true;effects_=effects;return true;
}
void PlatformOperation::end() {
  if (admission_ && lease_.generation) (void)admission_->release(kOperationOwner,lease_);
  admission_=nullptr;accepted_=false;effects_=0;
}
bool PlatformOperation::accepted() const { return accepted_; }
} // namespace lunaredu::lunaros
