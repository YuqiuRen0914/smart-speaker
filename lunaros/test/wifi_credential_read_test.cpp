// Include the implementation to exercise its private request parser without
// adding a production API solely for testing.
#include "device_api/projections/network_projection_handlers.cpp"
#include <cassert>

namespace {
unsigned config_reads = 0;
lunaredu::lunaros::DeviceConfig saved;
}
namespace lunaredu::lunaros {
DeviceConfig device_config() { ++config_reads; return saved; }
NetworkSnapshot network_snapshot() { return {}; }
ProvisioningWifiSnapshot provisioning_driver_wifi_snapshot() { return {}; }
int32_t provisioning_driver_wifi_rssi() { return 0; }
int provisioning_driver_wifi_channel() { return 0; }
void fill_device_identity_payload(JsonObject, const LocalDeviceIdentityConfig &) {}
bool save_device_network_config(const DeviceWifiCredential *, size_t,
                                const char *, const char *) { return true; }
bool request_network_apply_saved_credential(uint8_t, std::string &) { return true; }
bool clear_device_wifi_credentials() { return true; }
}
int main() {
  using namespace lunaredu::lunaros;
  saved.wifi.credential_count = 2;
  saved.wifi.credentials[0] = {"one", "original-one"};
  saved.wifi.credentials[1] = {"two", "original-two"};
  JsonDocument request;
  JsonArray entries = request["credentials"].to<JsonArray>();
  for (const char *ssid : {"one", "two"}) {
    JsonObject item = entries.add<JsonObject>();
    item["ssid"] = ssid;
    item["password"] = "new-password";
  }
  DeviceWifiCredential output[kDeviceWifiCredentialMax];
  assert(read_wifi_credentials(request.as<JsonVariantConst>(), output) == 2);
  assert(config_reads == 0);
  assert(output[0].password == "new-password");
  entries[0]["password_masked"] = true;
  entries[1]["password_masked"] = true;
  assert(read_wifi_credentials(request.as<JsonVariantConst>(), output) == 2);
  assert(config_reads == 1);
  assert(output[0].password == "original-one");
  assert(output[1].password == "original-two");
  entries[1]["ssid"] = "unknown";
  config_reads = 0;
  assert(read_wifi_credentials(request.as<JsonVariantConst>(), output) == 2);
  assert(config_reads == 1 && output[1].password.empty());
  saved.wifi.credentials[0] = {" Lab ", "spaced-secret"};
  saved.wifi.credentials[1] = {"Lab", "plain-secret"};
  entries[0]["ssid"] = " Lab "; entries[1]["ssid"] = "Lab";
  assert(read_wifi_credentials(request.as<JsonVariantConst>(), output) == 2);
  assert(output[0].ssid == " Lab " && output[0].password == "spaced-secret");
  assert(output[1].ssid == "Lab" && output[1].password == "plain-secret");
  request.clear(); config_reads = 0;
  assert(read_wifi_credentials(request.as<JsonVariantConst>(), output) == 0);
  assert(config_reads == 0);
}
