#include "network/bluetooth/bluetooth.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
#include <array>
#include <atomic>
#include <cstring>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
extern "C" void ble_store_config_init(void);
namespace lunaredu::lunaros {
namespace {
std::array<BluetoothProfile, 4> profiles{};
size_t count = 0;
bool running = false;
bool cleanup_pending = false;
bool sealed = false;
std::atomic<bool> synchronized{false};
void on_sync() {
  synchronized.store(true);
  for (size_t i = 0; i < count; ++i)
    if (profiles[i].synchronized) profiles[i].synchronized(profiles[i].context);
}
void on_reset(int) {
  synchronized.store(false);
  for (size_t i = 0; i < count; ++i)
    if (profiles[i].reset) profiles[i].reset(profiles[i].context);
}
void host_task(void *) { nimble_port_run(); nimble_port_freertos_deinit(); }
}
bool register_bluetooth_profile(const BluetoothProfile &profile, std::string &error) {
  if (!profile.id || !*profile.id || !profile.install) {
    error = "invalid_bluetooth_profile"; return false;
  }
  for (size_t i = 0; i < count; ++i) if (std::strcmp(profiles[i].id, profile.id) == 0) {
    const auto &p = profiles[i];
    if (p.install == profile.install && p.synchronized == profile.synchronized &&
        p.reset == profile.reset && p.context == profile.context &&
        p.stopped == profile.stopped) { error.clear(); return true; }
    error = "bluetooth_profile_conflict"; return false;
  }
  if (sealed || count == profiles.size()) { error = "bluetooth_profiles_closed"; return false; }
  profiles[count++] = profile; error.clear(); return true;
}
bool start_bluetooth(std::string &error) {
  if (running) { error.clear(); return true; }
  if (cleanup_pending) {
    auto cleanup = nimble_port_deinit();
    if (cleanup != ESP_OK) { error = esp_err_to_name(cleanup); return false; }
    cleanup_pending = false;
  }
  auto result = nimble_port_init();
  if (result != ESP_OK) { error = esp_err_to_name(result); return false; }
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
  for (size_t i = 0; i < count; ++i) {
    if (!profiles[i].install(profiles[i].context, error)) {
      cleanup_pending = nimble_port_deinit() != ESP_OK; return false;
    }
  }
  ble_store_config_init();
  result = esp_nimble_enable(reinterpret_cast<void *>(host_task));
  if (result != ESP_OK) {
    cleanup_pending = nimble_port_deinit() != ESP_OK; error = esp_err_to_name(result); return false;
  }
  running = true; sealed = true; error.clear(); return true;
}
bool stop_bluetooth(std::string &error) {
  if (!running && !cleanup_pending) { error.clear(); return true; }
  // Blocking stop is permitted only from the owner, never a NimBLE callback.
  if (running) {
    int result = nimble_port_stop();
    if (result != 0) { error = "bluetooth_stop_failed:" + std::to_string(result); return false; }
    running = false;
    cleanup_pending = true;
    synchronized.store(false);
    for (size_t i = 0; i < count; ++i)
      if (profiles[i].reset) profiles[i].reset(profiles[i].context);
    for (size_t i = 0; i < count; ++i)
      if (profiles[i].stopped) profiles[i].stopped(profiles[i].context);
  }
  auto deinit = nimble_port_deinit();
  if (deinit != ESP_OK) { error = esp_err_to_name(deinit); return false; }
  cleanup_pending = false;
  error.clear(); return true;
}
bool bluetooth_synchronized() { return synchronized.load(); }
}
#else
namespace lunaredu::lunaros {
bool register_bluetooth_profile(const BluetoothProfile &, std::string &e) { e = "bluetooth_disabled"; return false; }
bool start_bluetooth(std::string &e) { e = "bluetooth_disabled"; return false; }
bool stop_bluetooth(std::string &e) { e.clear(); return true; }
bool bluetooth_synchronized() { return false; }
}
#endif
