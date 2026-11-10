#include <cassert>
#include <atomic>
#include <cstring>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#endif
#include "network/wifi/wifi_manager.h"
#include "network/wifi/wifi_manager_internal.h"

WifiDriverState state;
WifiDriverState &wifi_driver_state() { return state; }
wifi_config_t applied{};
unsigned connect_calls = 0, config_calls = 0, scan_calls = 0;
std::atomic<bool> hold_scan{false}, scan_entered{false}, release_scan{false};
bool scan_ok = false;
std::vector<WifiScanApRecord> scan_result;
std::string scan_error;
int esp_wifi_set_mode(wifi_mode_t) { return 0; }
int esp_wifi_start() { return 0; }
int esp_wifi_disconnect() { return 0; }
int esp_wifi_set_config(int, const wifi_config_t *config) {
  applied = *config; ++config_calls; return 0;
}
int esp_wifi_connect() { ++connect_calls; return 0; }
int esp_wifi_scan_start(const wifi_scan_config_t *, bool) {
  ++scan_calls;
  if (hold_scan) {
    scan_entered = true;
    while (!release_scan) {
#ifdef _WIN32
      Sleep(1);
#else
      std::this_thread::yield();
#endif
    }
  }
  return 0;
}
int esp_wifi_scan_get_ap_num(uint16_t *count) { *count = 3; return 0; }
int esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *aps) {
  assert(*count >= 3);
  std::memset(aps, 0, sizeof(*aps) * 3);
  std::memset(aps[0].ssid, 's', 32);
  std::memcpy(aps[1].ssid, " Lab ", 5);
  std::memcpy(aps[2].ssid, "Lab", 3);
  for (int i = 0; i < 3; ++i) { aps[i].rssi = -30-i; aps[i].primary = 6; }
  return 0;
}
int esp_wifi_get_channel(uint8_t *channel, wifi_second_chan_t *) { *channel = 6; return 0; }
int esp_wifi_set_channel(uint8_t, wifi_second_chan_t) { return 0; }
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *) { return -1; }
bool wifi_driver_ensure_stack_ready() { return true; }
const char *wifi_disconnect_reason_name(int) { return "test"; }
namespace lunaredu::lunaros {
DeviceConfig device_config() { return {}; }
bool save_device_wifi_last_success_index(uint8_t) { return true; }
}
void scan() { scan_ok = wifi_scan_access_points(scan_result, scan_error); }
#ifdef _WIN32
DWORD WINAPI scan_thread(void *) { scan(); return 0; }
#endif
int main() {
  state.ssid = std::string(32, 's'); state.password = std::string(64, 'a');
  state.credential_count = 1; state.stack_ready = state.sta_started = true;
  wifi_driver_begin_connection();
  assert(connect_calls == 1 && config_calls == 1);
  assert(std::memcmp(applied.sta.ssid, state.ssid.data(), 32) == 0);
  assert(std::memcmp(applied.sta.password, state.password.data(), 64) == 0);
  std::vector<WifiScanApRecord> records; std::string error;
  assert(!wifi_scan_access_points(records, error));
  assert(error == "wifi_radio_busy" && scan_calls == 0);
  state.status = kWifiStatusDisconnected;
  state.ssid = " Lab "; state.password = " secret ";
  wifi_driver_begin_connection();
  assert(std::memcmp(applied.sta.ssid, " Lab ", 6) == 0);
  assert(std::memcmp(applied.sta.password, " secret ", 9) == 0);
  state.status = kWifiStatusDisconnected;
  hold_scan = true;
#ifdef _WIN32
  HANDLE scanner = CreateThread(nullptr, 0, scan_thread, nullptr, 0, nullptr);
  assert(scanner);
#else
  std::thread scanner(scan);
#endif
  while (!scan_entered) {}
  const auto before = connect_calls;
  wifi_driver_begin_connection();
  assert(connect_calls == before && config_calls == before);
  release_scan = true;
#ifdef _WIN32
  assert(WaitForSingleObject(scanner, 5000) == WAIT_OBJECT_0); CloseHandle(scanner);
#else
  scanner.join();
#endif
  assert(scan_ok && scan_result.size() == 3);
  assert(scan_result[0].ssid == std::string(32, 's'));
  assert(scan_result[1].ssid == " Lab " && scan_result[2].ssid == "Lab");
  wifi_driver_begin_connection(); assert(connect_calls == before + 1);
  lunaredu::lunaros::DeviceWifiCredential input[] = {{" Lab ", " secret "}, {"Lab", "plain"}};
  lunaredu::lunaros::DeviceWifiCredential output[lunaredu::lunaros::kDeviceWifiCredentialMax];
  uint8_t count = 0;
  assert(copy_wifi_credentials(input, 2, output, count) && count == 2);
  assert(output[0].ssid == " Lab " && output[0].password == " secret ");
  assert(output[1].ssid == "Lab");
  state.ssid = std::string(33, 's');
  wifi_driver_begin_connection(); assert(connect_calls == before + 1);
  std::cout << "Wi-Fi actual driver: full fields, exact SSIDs, scan/connect exclusion and retry passed\n";
}
