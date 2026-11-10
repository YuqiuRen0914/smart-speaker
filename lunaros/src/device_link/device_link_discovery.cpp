#include "device_link/device_link_operations.h"

#include <atomic>
#include <cstdio>
#include <ctime>

#include <ArduinoJson.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/persistence/identity.h"
#include "lunaredu_lunaros/network/network.h"
#include "device_link/device_link_http.h"
#include "kernel/system_primitives.h"
#include "device_link/device_link_state.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr const char *kTag = "device_link";

std::atomic<int64_t> g_discovery_expires_at_ms{0};
std::atomic<uint32_t> g_discovery_nonce{0};

} // namespace

bool publish_discovery_announcement() {
  const int64_t expires_at_ms = g_discovery_expires_at_ms.load();
  const int64_t now_epoch_ms = static_cast<int64_t>(time(nullptr)) * 1000;
  if (expires_at_ms <= now_epoch_ms) {
    g_discovery_expires_at_ms.store(0);
    return false;
  }

  const NetworkSnapshot network = network_snapshot();
  const DeviceConfig &config = device_config();
  const DeviceLinkConfig &runtime = runtime_config();
  char session_id[32] = {};
  std::snprintf(session_id, sizeof(session_id), "luna-%08lx",
                static_cast<unsigned long>(g_discovery_nonce.load()));

  JsonDocument doc;
  doc["discoveryEnabled"] = true;
  doc["discoverySessionId"] = session_id;
  doc["discoveryExpiresAtMs"] = expires_at_ms;
  doc["localAddress"] = network.local_ip;
  doc["controlBaseUrl"] = std::string("http://") + network.local_ip;
  doc["displayName"] = config.identity.name;
  doc["boardType"] =
      runtime.board_type.empty() ? "unknown" : runtime.board_type;
  doc["model"] =
      runtime.model.empty() ? "LunarEdu ESP32 Device" : runtime.model;
  doc["firmwareVersion"] = firmware_version();
  add_capabilities(doc["capabilities"].to<JsonArray>());
  std::string body;
  serializeJson(doc, body);

  int status_code = -1;
  if (!perform_request("POST", "/api/hardware/device-runtime/discovery",
                       &body, nullptr, &status_code)) {
    // Discovery is an address-refresh hint, not a claim channel. The server
    // may reject an expired/invalid announcement; stop a rejected window so
    // the device does not generate a retry storm.
    if (status_code == 409) {
      g_discovery_expires_at_ms.store(0);
    }
    return false;
  }
  return true;
}

bool begin_discovery(uint32_t duration_ms) {
  std::string clock_error;
  if (!ensure_network_clock(clock_error)) {
    LOGW(kTag, "failed to synchronize clock before LAN discovery: %s",
         clock_error.c_str());
    return false;
  }
  g_discovery_nonce.store(internal::system_random_u32());
  g_discovery_expires_at_ms.store(
      static_cast<int64_t>(time(nullptr)) * 1000 + duration_ms);
  LOGI(kTag, "LAN discovery enabled for %lu ms",
       static_cast<unsigned long>(duration_ms));
  return true;
}

bool discovery_active() {
  return g_discovery_expires_at_ms.load() > 0;
}

} // namespace lunaredu::lunaros::device_link
