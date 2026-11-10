#include "lunaredu_lunaros/persistence/identity.h"

#include "device_api/projections/identity_projection_internal.h"
#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::lunaros {
namespace {

void fill_firmware_identity(JsonObject payload) {
  payload["fw_version"] = lunaredu::lunaros::firmware_version();
}

} // 鍖垮悕鍛藉悕绌洪棿

void fill_device_identity_payload(JsonObject payload) {
  const DeviceConfig config = device_config();
  fill_device_identity_payload(payload, config.identity);
}

void fill_device_identity_payload(JsonObject payload,
                                   const LocalDeviceIdentityConfig &identity) {
  payload["serial_number"] = lunaredu::lunaros::hardware_serial_number();
  fill_firmware_identity(payload);
  payload["device_uuid"] = identity.local_device_uuid;
  payload["device_name"] = identity.name;
  payload["provision_ap_name"] = identity.provision_ap_name;
}

void fill_device_bootstrap_payload(JsonObject payload) {
  fill_device_identity_payload(payload);
}

void fill_device_status_payload(JsonObject payload) {
  fill_device_identity_payload(payload);
}

} // namespace lunaredu::lunaros
