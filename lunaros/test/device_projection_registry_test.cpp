#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>

#include "lunaredu_lunaros/device_api/device_projection.h"

using namespace lunaredu::lunaros;

namespace {

void fill_state(JsonObject) {}

void count_module(const DeviceModuleProjection &, void *context) {
  ++*static_cast<size_t *>(context);
}

void count_model_module(const lunaredu::lunaros::DeviceModuleSpec &,
                        void *context) {
  ++*static_cast<size_t *>(context);
}

} // namespace

int main() {
  setup_device_projection_registry();

  const DeviceOperationDefinition first_operations[] = {
      make_device_json_provider("status.get", fill_state),
  };
  const DeviceModuleProjectionMetadata first_metadata = {"sensor", "sensor"};
  const DeviceCapabilityDescriptor sensor_capability = {
      "sensor_capability", nullptr, 0};
  const DeviceModuleProjection first = {
      "sensor_capability", &first_metadata, first_operations, 1};

  assert(register_device_projection(
      {&sensor_capability, &first, nullptr, false}));
  assert(register_device_projection(
      {&sensor_capability, &first, nullptr, false}));
  assert(device_capability_declared("sensor_capability"));
  assert(!device_capability_active("sensor_capability"));
  assert(find_device_operation_projection("sensor", "status.get") ==
         &first_operations[0].projection);
  const auto *first_spec =
      lunaredu::lunaros::find_device_operation_spec("sensor", "status.get");
  assert(first_spec != nullptr);
  assert(first_spec->kind ==
         lunaredu::lunaros::DeviceOperationKind::Query);
  assert(!first_spec->mutating);
  assert(std::strcmp(first_spec->owner_executor,
                     lunaredu::lunaros::kMainServiceExecutor) == 0);
  assert(find_device_operation_projection("sensor", "missing") == nullptr);

  const DeviceOperationDefinition replacement_operations[] = {
      make_device_json_provider("sample.get", fill_state),
  };
  const DeviceModuleProjectionMetadata replacement_metadata = {"sensor", "sensor"};
  const DeviceModuleProjection replacement = {
      "sensor_capability", &replacement_metadata, replacement_operations, 1};
  std::string error;
  assert(!register_device_projection(
      {&sensor_capability, &replacement, nullptr, false}, &error));
  assert(error == "device_module_id_conflict");
  assert(find_device_operation_projection("sensor", "status.get") ==
         &first_operations[0].projection);
  assert(find_device_operation_projection("sensor", "sample.get") == nullptr);

  const DeviceOperationDefinition invalid_operations[] = {
      DeviceOperationDefinition{},
  };
  const DeviceModuleProjectionMetadata invalid_metadata = {"invalid", "sensor"};
  const DeviceModuleProjection invalid = {
      "invalid_capability", &invalid_metadata, invalid_operations, 1};
  const DeviceCapabilityDescriptor invalid_capability = {
      "invalid_capability", nullptr, 0};
  assert(!register_device_projection(
      {&invalid_capability, &invalid, nullptr, false}, &error));
  assert(error == "invalid_device_operation_id");
  assert(!device_capability_declared("invalid_capability"));
  const DeviceMetadataProjection invalid_status = {
      "invalid.status", "invalid", fill_state, nullptr};
  assert(!register_device_projection(
      {&invalid_capability, &invalid, &invalid_status, true}, &error));
  assert(!device_capability_declared("invalid_capability"));
  // Failed composition must not reserve the metadata id/section either.
  const DeviceMetadataProjection reused_status = {
      "invalid.status", "reused", fill_state, nullptr};
  assert(register_device_projection({nullptr, nullptr, &reused_status, false}, &error));

  // An already declared capability keeps its activation state on failure.
  assert(declare_device_capability(invalid_capability, &error));
  assert(!register_device_projection(
      {&invalid_capability, &invalid, nullptr, true}, &error));
  assert(device_capability_declared("invalid_capability"));
  assert(!device_capability_active("invalid_capability"));
  assert(activate_device_capability(invalid_capability, &error));
  assert(!register_device_projection(
      {&invalid_capability, &invalid, nullptr, false}, &error));
  assert(device_capability_active("invalid_capability"));

  const DeviceCapabilityDescriptor feature_capability = {
      "feature_capability", nullptr, 0};
  const DeviceOperationDefinition feature_operations[] = {
      make_device_json_provider("status.get", fill_state),
  };
  const DeviceModuleProjectionMetadata feature_metadata = {"feature", "feature"};
  const DeviceModuleProjection feature = {
      "feature_capability", &feature_metadata, feature_operations, 1};
  const DeviceMetadataProjection feature_status = {
      "feature.status", "feature", fill_state, nullptr};
  const DeviceProjectionContribution contribution = {
      &feature_capability, &feature, &feature_status, true};
  assert(register_device_projection(contribution, &error));
  assert(error.empty());
  assert(device_capability_active("feature_capability"));

  const DeviceCapabilityDescriptor wrong_capability = {
      "wrong_capability", nullptr, 0};
  assert(!register_device_projection(
      {&wrong_capability, &feature, nullptr, true}, &error));
  assert(error == "device_projection_capability_mismatch");

  assert(seal_device_projection_registry(&error));
  assert(device_projection_registry_sealed());
  assert(register_device_projection(
      {&sensor_capability, &first, nullptr, false}, &error));
  assert(activate_device_capability(sensor_capability, &error));
  assert(device_capability_active("sensor_capability"));
  assert(deactivate_device_capability("sensor_capability"));
  assert(device_capability_declared("sensor_capability"));
  assert(!device_capability_active("sensor_capability"));
  const DeviceModuleProjectionMetadata late_metadata = {"late", "late"};
  const DeviceModuleProjection late = {
      "late_capability", &late_metadata, feature_operations, 1};
  const DeviceCapabilityDescriptor late_capability = {
      "late_capability", nullptr, 0};
  assert(!register_device_projection(
      {&late_capability, &late, nullptr, false}, &error));
  assert(error == "capability_registry_sealed");
  assert(!activate_device_capability(late_capability, &error));
  assert(error == "capability_not_declared");

  size_t count = 0;
  visit_device_module_projections(count_module, &count);
  assert(count == 2);
  size_t model_count = 0;
  lunaredu::lunaros::visit_device_model_modules(count_model_module,
                                                 &model_count);
  assert(model_count == 2);

  // Failure at the final model commit must leave both earlier indexes intact.
  setup_device_projection_registry();
  const DeviceOperationSpec occupied_operation = first_operations[0].model;
  const DeviceModuleSpec occupied_model = {
      "feature", "different_kind", "other", &occupied_operation, 1};
  assert(register_device_model_module(occupied_model, &error));
  assert(!register_device_projection(contribution, &error));
  assert(error == "device_model_module_id_conflict");
  assert(!device_capability_declared("feature_capability"));
  assert(find_device_operation_projection("feature", "status.get") == nullptr);
  const DeviceMetadataProjection replacement_status = {
      "feature.status", "replacement", fill_state, nullptr};
  assert(register_device_projection({nullptr, nullptr, &replacement_status, false}, &error));
  assert(std::strcmp(find_device_module_spec("feature")->kind, "different_kind") == 0);
  return 0;
}
