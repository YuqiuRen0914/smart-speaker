#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include <iterator>
#include "device_api/projections/identity_projection_internal.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/kernel/text.h"

namespace lunaredu::lunaros {

namespace {
DeviceOperationResult set_name(JsonVariantConst request, JsonObject response) {
  if (!request["device_name"].is<const char *>())
    return device_operation_error("invalid_device_name", 400);
  const std::string name = text::trim_copy(std::string(request["device_name"].as<const char *>()));
  if (name.empty() || name.size() > kDeviceNameMax)
    return device_operation_error("invalid_device_name", 400);
  for (const unsigned char c : name)
    if (c < 0x20 || c == 0x7f) return device_operation_error("invalid_device_name", 400);
  if (!save_device_identity_config(name.c_str(), nullptr))
    return device_operation_error("identity_save_failed", 500);
  fill_device_identity_payload(response);
  return device_operation_ack();
}
const DeviceCapabilityDescriptor kIdentityCapability{"device_identity"};
constexpr DeviceOperationDefinition kIdentityOperations[] = {
  make_device_json_provider("status.get", static_cast<void (*)(JsonObject)>(fill_device_identity_payload)),
  make_device_json_command("name.set", set_name),
};
const DeviceModuleProjectionMetadata kIdentityModule{"identity", "identity"};
const DeviceModuleProjection kIdentityProjection{
  "device_identity", &kIdentityModule, kIdentityOperations, std::size(kIdentityOperations)};
}
bool setup_identity_projection(std::string &error) {
  static constexpr DeviceMetadataProjection kIdentityMetadata = {
      "device.identity", "identity", fill_device_identity_payload,
      fill_device_bootstrap_payload};
  return register_device_projection(
      {&kIdentityCapability, &kIdentityProjection, &kIdentityMetadata, true}, &error);
}

} // namespace lunaredu::lunaros
