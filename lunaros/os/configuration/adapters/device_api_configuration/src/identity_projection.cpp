#include "device_api/adapters/identity_projection.h"
#include "device_api/core_projections.h"
#include "device_api/device_operation_builders.h"
#include "model/device_model.h"
#include "device_api/device_api_context.h"
#include "system/identity.h"
#include "device_api/adapters/identity_payload.h"
#include "configuration/device_config.h"
#include "device_api/device_projection.h"
#include <iterator>
#include "platform/text.h"

namespace lunaredu::lunaros {
namespace {

void fill_firmware_identity(JsonObject payload) {
  payload["fw_version"] = lunaredu::lunaros::firmware_version();
}

} // namespace

void fill_device_identity_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  fill_device_identity_payload(payload, device_identity_config());
}

void fill_device_identity_payload(JsonObject payload,
                                   const LocalDeviceIdentityConfig &identity) {
  payload["serial_number"] = lunaredu::lunaros::hardware_serial_number();
  fill_firmware_identity(payload);
  payload["device_uuid"] = identity.local_device_uuid;
  payload["device_name"] = identity.name;
  payload["provision_ap_name"] = identity.provision_ap_name;
}

namespace {
DeviceOperationResult set_name([[maybe_unused]] DeviceApiContext &device, JsonVariantConst request, JsonObject response) {
  if (!request["device_name"].is<const char *>())
    return device_operation_error("invalid_device_name", 400);
  const JsonString raw=request["device_name"].as<JsonString>();
  if(raw.size()>kDeviceNameMax || std::string_view(raw.c_str(),raw.size()).find('\0')!=std::string_view::npos)
    return device_operation_error("invalid_device_name",400);
  const std::string name = text::trim_copy(std::string(raw.c_str(),raw.size()));
  if (name.empty() || name.size() > kDeviceNameMax)
    return device_operation_error("invalid_device_name", 400);
  for (const unsigned char c : name)
    if (c < 0x20 || c == 0x7f) return device_operation_error("invalid_device_name", 400);
  if (!save_device_identity_config(name.c_str(), nullptr))
    return device_operation_error("identity_save_failed", 500);
  fill_device_identity_payload(device, response);
  return device_operation_ack();
}
const DeviceCapabilityDescriptor kIdentityCapability{"device_identity"};
constexpr DeviceOperationDefinition kIdentityOperations[] = {
  device_operation_remote(make_device_json_provider("status.get", static_cast<void (*)(DeviceApiContext &, JsonObject)>(fill_device_identity_payload)), DeviceRemoteAccess::Query),
  device_operation_remote(make_device_json_command("name.set", set_name), DeviceRemoteAccess::Command),
};
const DeviceModuleProjectionMetadata kIdentityModule{"identity", "identity"};
const DeviceModuleProjection kIdentityProjection{
  "device_identity", &kIdentityModule, kIdentityOperations, std::size(kIdentityOperations)};
}
static bool install_identity_projection(DeviceProjectionRegistry &registry, std::string &error) {
  static constexpr DeviceMetadataProjection kIdentityMetadata = {
      "device.identity", "identity", fill_device_identity_payload,
      fill_device_identity_payload};
  return registry.register_projection(
      {&kIdentityCapability, &kIdentityProjection, &kIdentityMetadata, true}, &error);
}

const StandardProjectionContribution standard_projection::identity{"identity", install_identity_projection};

} // namespace lunaredu::lunaros
