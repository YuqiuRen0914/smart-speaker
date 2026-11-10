#pragma once
#include "app/execution/device_basic_abi.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <span>
#include "model/operation_outcome.h"

namespace lunaredu::lunaros::apps {
struct ApplicationDeviceRequest {int32_t operation=0,a=0,b=0,c=0;};
struct ApplicationDeviceContract {
  int32_t operation;
  const char *id;
  const char *unit;
  int32_t minimum,maximum;
  bool writes_output;
  uint8_t read_channels;
};
#define LUNA_DEVICE_DESCRIPTOR(op,id,unit,lo,hi,writes,channels) {op,id,unit,lo,hi,bool(writes),channels},
inline constexpr ApplicationDeviceContract kApplicationDeviceContracts[] = {
  LUNA_DEVICE_CONTRACTS(LUNA_DEVICE_DESCRIPTOR)
};
#undef LUNA_DEVICE_DESCRIPTOR
inline std::span<const ApplicationDeviceContract> application_device_contracts() {
  return kApplicationDeviceContracts;
}
// One source for native discovery, protocol adapters and host admission.
const ApplicationDeviceContract *application_device_contract(int32_t operation);
bool application_device_value_valid(int32_t operation,int32_t value);
struct ApplicationDeviceOutcome {
  OperationCompletion completion=OperationCompletion::Completed;
  OperationError category=OperationError::None;
};
inline constexpr auto kApplicationDeviceWriteAcknowledgement=OperationCompletion::Accepted;
inline constexpr auto kApplicationDeviceReadAcknowledgement=OperationCompletion::Completed;
// Writes returning OK are queued acceptance. Reads returning a scalar are
// completed observations. Neither is proof of physical output retirement.
ApplicationDeviceOutcome application_device_outcome(const ApplicationDeviceRequest &,int32_t);
struct ApplicationDeviceIdentity {
  const char *application_id=nullptr; // Host-owned, not guest supplied.
  uint64_t instance=0;
};
struct ApplicationDevicePorts {
  void *context=nullptr;
  uint32_t capabilities=0; // Declared contract bits, not runtime readiness.
  // All provider callbacks run on ApplicationsService's Main owner. They must
  // be bounded: cache reads and admitted commands only; no network waits or
  // unbounded driver waits. A synchronous SDK transfer must have a finite limit.
  bool (*admit)(void *,const ApplicationDeviceIdentity &,std::string &)=nullptr;
  bool (*authorized)(void *,const ApplicationDeviceIdentity &)=nullptr;
  uint32_t (*available)(void *,const ApplicationDeviceIdentity &)=nullptr;
  int32_t (*invoke)(void *,const ApplicationDeviceIdentity &,const ApplicationDeviceRequest &)=nullptr;
  // Stops only this instance's outputs/subscriptions. False keeps identity,
  // leases and callback contexts alive; Main will retry before reuse.
  bool (*retire)(void *,const ApplicationDeviceIdentity &,std::string &)=nullptr;
  const char *provider_service_id=nullptr; // Boot-lived lifecycle edge.
  // Optional monotonic clock for a host/simulator using this exact device
  // owner. Bounded, non-reentrant and immutable with the other ports; uint32
  // wrap has the same elapsed-time contract as firmware milliseconds.
  uint32_t (*monotonic_ms)(void *)=nullptr;
  void *clock_context=nullptr;
};
struct SessionDevice {
  void *context=nullptr;
  uint64_t instance=0;
  bool permitted=false;
  int32_t (*call)(void *,uint64_t,const ApplicationDeviceRequest &)=nullptr;
  void (*revoke)(void *,uint64_t)=nullptr;
  bool (*retire)(void *,uint64_t,std::string &)=nullptr;
};
// Backend-independent device admission for one application instance. All
// methods run on its VM owner; provider callbacks retain their original owner
// contracts. A failed retirement keeps the complete binding for retry.
class SessionDeviceScope {
public:
  SessionDeviceScope()=default;
  SessionDeviceScope(const SessionDeviceScope &)=delete;
  SessionDeviceScope &operator=(const SessionDeviceScope &)=delete;
  bool bind(SessionDevice,bool validation_only,std::string &);
  void begin_callback(bool permits_effects);
  void end_callback();
  int32_t call(const ApplicationDeviceRequest &,bool cancelled=false);
  bool budget_exhausted() const { return calls_>LUNA_DEVICE_CALLBACK_BUDGET; }
  void revoke();
  bool retire(std::string &);
  bool retained() const { return bound_; }
private:
  SessionDevice device_{};
  unsigned calls_=0;
  bool bound_=false,validation_only_=false,revoked_=false,calling_=false;
};
struct ApplicationDeviceStatus {
  uint32_t contract_version=LUNA_DEVICE_CONTRACT_VERSION;
  uint32_t declared=0,available=0,pending=0;
  uint64_t instance=0;
  int32_t last_result=LUNA_DEVICE_OK;
  bool permitted=false,revoked=false,retired=true;
  OperationCompletion last_completion=OperationCompletion::NotStarted;
  OperationError last_error_category=OperationError::None;
};
// One foreground instance, owned by ApplicationsService. Guest calls copy
// scalar writes into eight fixed slots; Main executes at most four per poll.
// Reads use bounded cached samples. No task or global capability registry.
class ApplicationDeviceHost {
public:
  ApplicationDeviceHost();
  ~ApplicationDeviceHost();
  ApplicationDeviceHost(const ApplicationDeviceHost &)=delete;
  ApplicationDeviceHost &operator=(const ApplicationDeviceHost &)=delete;
  bool configure(const ApplicationDevicePorts &,std::string &);
  bool begin(const std::string &id,bool permission,std::string &);
  SessionDevice binding(bool verified_permission);
  void poll(); // Main only, also while the VM task is stopping/trapped.
  void revoke(); // Thread safe, closes admission without invoking providers.
  bool retired() const;
  const std::string &failure() const;
  const char *provider_service_id() const;
  ApplicationDeviceStatus status() const; // Copied scalar observation, no I/O.
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
