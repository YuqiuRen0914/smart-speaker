#include "device_api/projections/wifi_config_payload.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include <cassert>
#include <string>
int main() {
  using namespace lunaredu::lunaros;
  DeviceWifiConfig wifi;
  wifi.configured = true;
  wifi.credential_count = 2;
  wifi.last_success_index = 1;
  wifi.credentials[0] = {"private-network", "must-not-leak-password"};
  wifi.credentials[1] = {"open-network", ""};
  for (bool provisioning : {false, true}) {
    JsonDocument doc;
    fill_saved_wifi_config_payload(doc.to<JsonObject>(), wifi, provisioning);
    assert(doc["configured"] == true);
    assert(doc["last_success_index"] == 1);
    assert(doc["credentials"].size() == 2);
    assert(doc["credentials"][0]["password"] == "");
    assert(doc["credentials"][0]["password_set"] == true);
    assert(doc["credentials"][0]["password_masked"] == true);
    assert(doc["credentials"][1]["password_set"] == false);
    assert(doc["credentials"][1]["password_masked"] == provisioning);
    if (provisioning) assert(doc["credentials"][0]["password_visible"] == false);
    else assert(doc["credentials"][0]["password_visible"].isNull());
    std::string json;
    serializeJson(doc, json);
    assert(json.find("must-not-leak-password") == std::string::npos);
  }
  wifi.credential_count = 0;
  wifi.configured = false;
  JsonDocument empty;
  fill_saved_wifi_config_payload(empty.to<JsonObject>(), wifi);
  assert(empty["credentials"].size() == 0 && empty["configured"] == false);
}
