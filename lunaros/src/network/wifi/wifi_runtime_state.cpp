#include "network/wifi/wifi_manager_internal.h"

#include "network/wifi/wifi_reconnect_policy.h"

WifiDriverState &wifi_driver_state() {
  // Static storage keeps the ESP32 Wi-Fi backend allocation-free and does not create
  // an additional FreeRTOS task. Construction is completed before first use.
  static WifiDriverState state;
  return state;
}

uint32_t wifi_driver_reconnect_delay(uint32_t attempts) {
  return wifi_reconnect_delay_for_attempts(attempts);
}

bool wifi_driver_connected_locked() {
  return wifi_driver_state().status == kWifiStatusConnected;
}
