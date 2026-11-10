#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>

#include "lunaredu_lunaros/model/device_model.h"

namespace {

void count_module(const lunaredu::lunaros::DeviceModuleSpec &, void *context) {
  ++*static_cast<size_t *>(context);
}

} // namespace

int main() {
  using namespace lunaredu::lunaros;

  setup_device_model_registry();
  const DeviceOperationSpec operations[] = {
      {"status.get", DeviceOperationKind::Query, false, "lunar.main", nullptr,
       "status.v1"},
      {"restart", DeviceOperationKind::Command, true, "lunar.main",
       "restart.v1", "ack.v1"},
  };
  const DeviceModuleSpec system = {
      "system", "service", "system_runtime", operations, 2};
  std::string error;
  assert(register_device_model_module(system, &error));
  assert(error.empty());
  assert(register_device_model_module(system, &error));

  const DeviceOperationSpec conflicting_operations[] = {
      {"status.get", DeviceOperationKind::Command, true, "lunar.main"},
  };
  const DeviceModuleSpec conflicting = {
      "system", "service", "system_runtime", conflicting_operations, 1};
  assert(!register_device_model_module(conflicting, &error));
  assert(error == "device_model_module_id_conflict");

  const DeviceOperationSpec invalid_operations[] = {
      {"invalid", DeviceOperationKind::Query, false, nullptr},
  };
  const DeviceModuleSpec invalid = {
      "invalid", "service", "invalid", invalid_operations, 1};
  assert(!register_device_model_module(invalid, &error));
  assert(error == "device_operation_owner_missing");

  const DeviceOperationSpec *restart =
      find_device_operation_spec("system", "restart");
  const DeviceModuleSpec *registered_module =
      find_device_module_spec("system");
  assert(registered_module != nullptr);
  assert(std::strcmp(registered_module->capability_id,
                     "system_runtime") == 0);
  assert(restart != nullptr);
  assert(restart->mutating);
  assert(std::strcmp(restart->request_schema, "restart.v1") == 0);

  size_t count = 0;
  visit_device_model_modules(count_module, &count);
  assert(count == 1);
  assert(seal_device_model_registry(&error));
  assert(device_model_registry_sealed());

  const DeviceModuleSpec late = {
      "late", "service", "late", operations, 1};
  assert(!register_device_model_module(late, &error));
  assert(error == "device_model_registry_sealed");
  return 0;
}
