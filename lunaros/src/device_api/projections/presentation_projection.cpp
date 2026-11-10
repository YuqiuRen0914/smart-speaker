#include "lunaredu_lunaros/device_api/presentation_projection.h"

#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/model/presentation_contract.h"

namespace lunaredu::lunaros {
namespace {

constexpr const char *kModuleId = "presentation";
PresentationProjectionConfig g_config;

DeviceOperationResult fill_snapshot(JsonObject payload) {
  if (g_config.snapshot_provider == nullptr) {
    return device_operation_error("presentation_provider_unavailable", 503,
                                     "presentation_unavailable");
  }
  return g_config.snapshot_provider(payload);
}

DeviceOperationResult fill_state(JsonObject payload) {
  if (g_config.state_provider == nullptr) {
    return device_operation_error("presentation_provider_unavailable", 503,
                                     "presentation_unavailable");
  }
  return g_config.state_provider(payload);
}

DeviceOperationResult handle_intent(JsonVariantConst request,
                                       JsonObject response) {
  if (g_config.intent_handler == nullptr) {
    return device_operation_error("presentation_intent_unavailable", 503,
                                     "presentation_control_failed");
  }
  return g_config.intent_handler(request, response);
}

void fill_contract(JsonObject contract) {
  contract["id"] = kPresentationContractId;
  contract["version"] = kPresentationContractVersion;
  contract["schema_hash"] = kPresentationSchemaSha256;
  contract["renderer_neutral"] = true;
  contract["semantic_intents"] = true;
  contract["authoritative_device_state"] = true;
}

const DeviceCapabilityDescriptor kPresentationCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "presentation";
  return descriptor;
}();

constexpr DeviceOperationDefinition kOperations[] = {
    make_device_json_query("get", fill_snapshot),
    make_device_json_query("state.get", fill_state),
    make_device_json_command("intent", handle_intent),
};

const DeviceModuleProjectionMetadata kModule = {
    kModuleId,
    "presentation",
    fill_contract,
};

const DeviceModuleProjection kSurface = {
    "presentation",
    &kModule,
    kOperations,
    sizeof(kOperations) / sizeof(kOperations[0]),
};

} // namespace

bool setup_presentation_projection(
    const PresentationProjectionConfig &config) {
  if (config.snapshot_provider == nullptr || config.state_provider == nullptr ||
      config.intent_handler == nullptr) {
    return false;
  }
  g_config = config;
  return register_device_projection(
      {&kPresentationCapability, &kSurface, nullptr, false});
}

bool set_presentation_projection_available(bool available,
                                           std::string &error) {
  error.clear();
  if (!available) {
    (void)deactivate_device_capability(kPresentationCapability.id);
    return true;
  }
  return activate_device_capability(kPresentationCapability, &error);
}

} // namespace lunaredu::lunaros
