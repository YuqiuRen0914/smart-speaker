#include "lunaredu_lunaros/network/provisioning_ble.h"
#include "network/bluetooth/bluetooth.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#if defined(CONFIG_LUNAREDU_NETWORK_ENABLE_BLE_PROVISIONING) && \
    CONFIG_LUNAREDU_NETWORK_ENABLE_BLE_PROVISIONING

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <services/gap/ble_svc_gap.h>


namespace lunaredu::lunaros {
namespace {

constexpr const char *kTag = "lunaros_ble_provision";
constexpr size_t kDeviceNameSize = 32;

// UUID string: f03c9ea1-4d77-4f2d-906a-704c425a0101.
ble_uuid128_t g_service_uuid = BLE_UUID128_INIT(
    0x01, 0x01, 0x5a, 0x42, 0x4c, 0x70, 0x6a, 0x90, 0x2d, 0x4f, 0x77, 0x4d,
    0xa1, 0x9e, 0x3c, 0xf0);
ble_uuid128_t g_rx_uuid = BLE_UUID128_INIT(
    0x02, 0x01, 0x5a, 0x42, 0x4c, 0x70, 0x6a, 0x90, 0x2d, 0x4f, 0x77, 0x4d,
    0xa1, 0x9e, 0x3c, 0xf0);
ble_uuid128_t g_tx_uuid = BLE_UUID128_INIT(
    0x03, 0x01, 0x5a, 0x42, 0x4c, 0x70, 0x6a, 0x90, 0x2d, 0x4f, 0x77, 0x4d,
    0xa1, 0x9e, 0x3c, 0xf0);

ProvisioningBleConfig g_config;
ProvisioningFrameDecoder g_decoder;
std::array<char, kDeviceNameSize> g_device_name{};
std::vector<uint8_t> g_last_response;
uint16_t g_rx_handle = 0;
uint16_t g_tx_handle = 0;
std::atomic<uint16_t> g_conn_handle{BLE_HS_CONN_HANDLE_NONE};
bool g_initialized = false;
std::atomic<bool> g_running{false};
std::atomic<bool> g_stopping{false};
std::atomic<uint16_t> g_disconnect_requested{BLE_HS_CONN_HANDLE_NONE};
int64_t g_retry_at_ms = 0;
std::string g_last_error;

bool connection_is_encrypted(uint16_t conn_handle) {
  ble_gap_conn_desc desc{};
  return ble_gap_conn_find(conn_handle, &desc) == 0 &&
         desc.sec_state.encrypted != 0;
}

void response_error(const ProvisioningFrame &request,
                    ProvisioningFrame &response) {
  response.version = kProvisioningProtocolVersion;
  response.type = static_cast<uint8_t>(ProvisioningFrameType::Error) |
                  kProvisioningFrameResponseFlag;
  response.request_id = request.request_id;
  response.payload = R"({"ok":false,"error":"bootstrap_unsupported"})";
}

void notify_response(const ProvisioningFrame &response) {
  std::vector<uint8_t> encoded;
  if (!encode_provisioning_frame(response, encoded)) {
    ESP_LOGW(kTag, "failed to encode response request_id=%lu",
             static_cast<unsigned long>(response.request_id));
    return;
  }
  g_last_response = encoded;
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || g_tx_handle == 0) return;
  struct os_mbuf *om = ble_hs_mbuf_from_flat(encoded.data(), encoded.size());
  if (om == nullptr) {
    ESP_LOGW(kTag, "failed to allocate notification request_id=%lu",
             static_cast<unsigned long>(response.request_id));
    return;
  }
  const int rc = ble_gatts_notify_custom(g_conn_handle, g_tx_handle, om);
  if (rc != 0) {
    ESP_LOGW(kTag, "notification failed rc=%d request_id=%lu", rc,
             static_cast<unsigned long>(response.request_id));
    // NimBLE consumes om on both success and failure.
  }
}

void handle_write(uint16_t conn_handle, struct os_mbuf *om) {
  if (!connection_is_encrypted(conn_handle)) return;
  const uint16_t length = OS_MBUF_PKTLEN(om);
  std::vector<uint8_t> bytes(length);
  uint16_t copied = 0;
  if (ble_hs_mbuf_to_flat(om, bytes.data(), length, &copied) != 0 ||
      copied != length) {
    ESP_LOGW(kTag, "failed to read provisioning write length=%u",
             static_cast<unsigned>(length));
    return;
  }

  std::vector<ProvisioningFrame> frames;
  g_decoder.append(bytes.data(), bytes.size(), frames);
  for (const ProvisioningFrame &request : frames) {
    ProvisioningFrame response;
    bool handled = false;
    if (g_config.frame_handler != nullptr) {
      handled = g_config.frame_handler(request, response, g_config.user_context);
    }
    if (!handled) response_error(request, response);
    notify_response(response);
  }
}

int access_callback(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (!g_running.load()) return BLE_ATT_ERR_UNLIKELY;
  (void)attr_handle;
  (void)arg;
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    if (!g_last_response.empty() &&
        os_mbuf_append(ctxt->om, g_last_response.data(),
                       g_last_response.size()) != 0) {
      return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
  }
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  handle_write(conn_handle, ctxt->om);
  return 0;
}

struct ble_gatt_chr_def g_characteristics[3] = {};
struct ble_gatt_svc_def g_services[2] = {};

esp_err_t start_advertising();

void init_gatt_defs() {
  g_characteristics[0].uuid = &g_rx_uuid.u;
  g_characteristics[0].access_cb = access_callback;
  g_characteristics[0].flags =
      BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
      BLE_GATT_CHR_F_WRITE_ENC;
  g_characteristics[0].val_handle = &g_rx_handle;
  g_characteristics[1].uuid = &g_tx_uuid.u;
  g_characteristics[1].access_cb = access_callback;
  g_characteristics[1].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                               BLE_GATT_CHR_F_NOTIFY |
                               BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC;
  g_characteristics[1].val_handle = &g_tx_handle;
  g_services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
  g_services[0].uuid = &g_service_uuid.u;
  g_services[0].characteristics = g_characteristics;
}

int gap_event_callback(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        g_conn_handle = event->connect.conn_handle;
        if (!g_running.load()) {
          g_stopping.store(true);
          return 0;
        }
        ESP_LOGI(kTag, "BLE provisioning connected handle=%u",
                 static_cast<unsigned>(g_conn_handle));
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      g_disconnect_requested = BLE_HS_CONN_HANDLE_NONE;
      g_decoder.reset();
      g_last_response.clear();
      return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      return 0;
    default:
      return 0;
  }
}

esp_err_t start_advertising() {
  if (!g_running.load() || ble_gap_adv_active()) return ESP_OK;
  ble_hs_adv_fields fields{};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  // Flags plus the 128-bit service UUID fit the 31-byte legacy packet.
  // The product name belongs in the scan response.
  ble_hs_adv_fields scan{};
  scan.name = reinterpret_cast<uint8_t *>(g_device_name.data());
  scan.name_len = std::min<size_t>(std::strlen(g_device_name.data()), 29);
  scan.name_is_complete = scan.name_len == std::strlen(g_device_name.data());
  if (ble_gap_adv_rsp_set_fields(&scan) != 0) return ESP_FAIL;
  fields.uuids128 = &g_service_uuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) return ESP_FAIL;

  ble_gap_adv_params params{};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  params.itvl_min = 0x20;
  params.itvl_max = 0x30;
  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                         &params, gap_event_callback, nullptr);
  return rc == 0 ? ESP_OK : ESP_FAIL;
}

bool install_gatt(void *, std::string &error) {
  init_gatt_defs();
  int rc = ble_gatts_count_cfg(g_services);
  if (rc == 0) rc = ble_gatts_add_svcs(g_services);
  if (rc != 0) { error = "provisioning_gatt_failed:" + std::to_string(rc); return false; }
  error.clear(); return true;
}
void on_sync(void *) {
  // GAP mutations belong to the owner poll, never two concurrent callbacks.
}
void on_reset(void *) {
  g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  g_disconnect_requested = BLE_HS_CONN_HANDLE_NONE;
  g_decoder.reset();
  g_last_response.clear();
}
void on_stopped(void *) {
  g_running.store(false);
  g_stopping.store(false);
  g_disconnect_requested = BLE_HS_CONN_HANDLE_NONE;
  g_last_error.clear();
}

bool reconcile(std::string &error) {
  g_retry_at_ms = esp_timer_get_time() / 1000 + 250;
  if (!g_running.load() && g_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE)
    g_stopping.store(true);
  if (g_stopping.load()) {
    if (bluetooth_synchronized() && ble_gap_adv_active()) {
      const int rc = ble_gap_adv_stop();
      if (rc != 0 && rc != BLE_HS_EALREADY) {
        error = "ble_advertising_stop_failed:" + std::to_string(rc); return false;
      }
    }
    const uint16_t connection = g_conn_handle.load();
    if (connection != BLE_HS_CONN_HANDLE_NONE) {
      if (g_disconnect_requested != connection) {
        const int rc = ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        if (rc == BLE_HS_ENOTCONN) g_conn_handle.store(BLE_HS_CONN_HANDLE_NONE);
        else if (rc != 0 && rc != BLE_HS_EALREADY) {
          error = "ble_disconnect_failed:" + std::to_string(rc); return false;
        } else g_disconnect_requested = connection;
      }
      if (g_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE) {
        error = "ble_disconnect_pending"; return false;
      }
    }
    g_disconnect_requested = BLE_HS_CONN_HANDLE_NONE;
    g_stopping.store(false);
    error.clear(); return true;
  }
  if (!g_running.load()) { error.clear(); return true; }
  if (!start_bluetooth(error)) return false;
  if (!bluetooth_synchronized()) { error.clear(); return true; }
  if (g_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE) { error.clear(); return true; }
  const int name_result = ble_svc_gap_device_name_set(g_device_name.data());
  if (name_result != 0) {
    error = "ble_device_name_failed:" + std::to_string(name_result); return false;
  }
  if (start_advertising() != ESP_OK) { error = "ble_advertising_failed"; return false; }
  error.clear(); return true;
}

bool reconcile_and_record(std::string &error) {
  const bool ok = reconcile(error);
  g_last_error = error;
  return ok;
}

}  // namespace

bool provisioning_ble_start(const ProvisioningBleConfig &config,
                            std::string &error) {
  if (g_stopping.load()) {
    if (!reconcile_and_record(error)) return false;
  }
  if (g_initialized && (g_config.frame_handler != config.frame_handler ||
                       g_config.user_context != config.user_context)) {
    error = "ble_provisioning_config_changed"; return false;
  }
  if (!g_initialized) {
    g_config = config;
    const char *prefix = config.device_name_prefix == nullptr ||
                                 config.device_name_prefix[0] == '\0'
                             ? "LunarOS"
                             : config.device_name_prefix;
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) {
      (void)esp_read_mac(mac, ESP_MAC_BASE);
  }
  std::snprintf(g_device_name.data(), g_device_name.size(), "%s-%02X%02X",
                prefix, mac[4], mac[5]);
  }
  if (!g_initialized) {
    static const BluetoothProfile profile{"provisioning", install_gatt, on_sync, on_reset, nullptr, on_stopped};
    if (!register_bluetooth_profile(profile, error)) return false;
    g_initialized = true;
  }
  g_running.store(true);
  return reconcile_and_record(error);
}

bool provisioning_ble_stop(std::string &error) {
  g_running.store(false);
  g_stopping.store(true);
  return reconcile_and_record(error);
}

bool provisioning_ble_poll(std::string &error) {
  if (esp_timer_get_time() / 1000 < g_retry_at_ms) {
    error = g_last_error; return error.empty();
  }
  return reconcile_and_record(error);
}
bool provisioning_ble_running() {
  return g_running.load() && bluetooth_synchronized() &&
      (g_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active());
}
ProvisioningBleSnapshot provisioning_ble_snapshot() {
  using State = ProvisioningBleState;
  State state;
  if (g_stopping.load()) state = State::Stopping;
  else if (!g_running.load()) state = State::Stopped;
  else if (!g_last_error.empty()) state = State::Failed;
  else if (!bluetooth_synchronized()) state = State::WaitingForHost;
  else if (g_conn_handle.load() != BLE_HS_CONN_HANDLE_NONE) state = State::Connected;
  else state = ble_gap_adv_active() ? State::Advertising : State::Starting;
  return {state, g_last_error};
}

}  // namespace lunaredu::lunaros

#else

namespace lunaredu::lunaros {

bool provisioning_ble_start(const ProvisioningBleConfig &, std::string &error) {
  error = "ble_provisioning_not_supported";
  return false;
}

bool provisioning_ble_stop(std::string &error) {
  error = "ble_provisioning_not_supported";
  return false;
}

bool provisioning_ble_running() { return false; }
bool provisioning_ble_poll(std::string &error) { error.clear(); return true; }
ProvisioningBleSnapshot provisioning_ble_snapshot() { return {}; }

}  // namespace lunaredu::lunaros

#endif
