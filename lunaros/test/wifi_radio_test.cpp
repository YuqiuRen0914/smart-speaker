#include <cassert>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#endif
#include <atomic>
#include "network/wifi/wifi_manager_internal.h"
#include "network/wifi/wifi_manager.h"
WifiDriverState state;
WifiDriverState &wifi_driver_state() { return state; }
int radio_channel = 6, retunes = 0;
bool driver_started = true, driver_associated = false;
int esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *) {
  *primary = radio_channel; return driver_started ? 0 : -1;
}
int esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t) {
  radio_channel = primary; ++retunes; return 0;
}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *) { return driver_associated ? 0 : -1; }
std::atomic<bool> scanning{false}, done{false};
void hold_scan_radio() {
  std::lock_guard<std::recursive_mutex> lock(wifi_radio_mutex());
  scanning = true; while (!done) {}
}
#ifdef _WIN32
DWORD WINAPI scanner_thread(void *) { hold_scan_radio(); return 0; }
#endif
int main() {
  std::string error;
  // Radio-only operation has a channel even without an AP association.
  assert(wifi_channel() == 6);
  state.stack_ready = true; state.sta_started = true;
  state.station_enabled = false; state.station_suspended = true;
  assert(wifi_set_radio_channel(11,error) && retunes == 1);
  state.station_enabled = true; state.station_suspended = false;
  state.credential_count = 1; state.status = kWifiStatusIdle;
  assert(!wifi_set_radio_channel(1,error) && error == "wifi_radio_busy");
  state.status = kWifiStatusConnected; state.station_connected = true;
  assert(!wifi_set_radio_channel(1,error) && error == "wifi_radio_channel_in_use");
  assert(wifi_set_radio_channel(11,error) && retunes == 1);
  state.station_connected = false; state.status = kWifiStatusDisconnected;
  // The SDK may already be associated before its queued event is processed.
  driver_associated = true;
  assert(!wifi_set_radio_channel(1,error)); driver_associated = false;
  state.softap_active = true;
  assert(!wifi_set_radio_channel(1,error)); state.softap_active = false;
#ifdef _WIN32
  HANDLE scanner = CreateThread(nullptr, 0, scanner_thread, nullptr, 0, nullptr);
  assert(scanner != nullptr);
#else
  std::thread scanner(hold_scan_radio);
#endif
  while (!scanning) {}
  assert(!wifi_set_radio_channel(1,error) && error == "wifi_radio_busy");
  done = true;
#ifdef _WIN32
  assert(WaitForSingleObject(scanner, 5000) == WAIT_OBJECT_0);
  CloseHandle(scanner);
#else
  scanner.join();
#endif
  assert(wifi_set_radio_channel(1,error) && retunes == 2);
  driver_started = false;
  assert(wifi_channel() == 0 && !wifi_set_radio_channel(1,error));
  std::cout << "Wi-Fi radio-only, coexistence and scan arbitration tests passed\n";
}
