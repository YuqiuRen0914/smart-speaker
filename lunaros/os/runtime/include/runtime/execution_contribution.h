#pragma once
#include "kernel/service.h"
#include "kernel/service_executor.h"
#include <memory>
#include <string>
namespace lunaredu::lunaros {
struct DeviceContext;
struct RuntimeExecutionPorts {
  void *foreground_context = nullptr;
  bool (*foreground_transition)(void *,bool,std::string &) = nullptr;
};
// A declaration owner, never a second service lifecycle. Its node and borrowed
// context survive until reboot; ServiceHost still starts/stops the actual owner.
class RuntimeExecutionOwner {
public:
  virtual ~RuntimeExecutionOwner() = default;
  virtual ServiceNode node() = 0;
};
struct RuntimeExecutionContribution {
  const char *id = nullptr;
  // If this service is in the actual graph, ApplicationsService depends on it
  // so guests must retire before the engine can stop. No service name guesses.
  const char *service_id = nullptr;
  bool (*configure_backend)(std::string &) = nullptr;
  std::unique_ptr<RuntimeExecutionOwner> (*create_default)(DeviceContext &,RuntimeExecutionPorts) = nullptr;
  ServiceExecutorDefinition executor{};
};
}
