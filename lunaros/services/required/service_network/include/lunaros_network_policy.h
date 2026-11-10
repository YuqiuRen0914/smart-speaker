#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "lunaros_network_job_policy.h"
#include "lunaros_network_espnow_policy.h"

namespace lunaredu::lunaros {
inline constexpr size_t kNetworkInitialWifiCredentialCapacity = 5;
inline constexpr char kNetworkDefaultWifiSsid[] = "lunaredu";
inline constexpr char kNetworkDefaultWifiPassword[] = "lunaredu";

struct NetworkInitialWifiCredential {
  std::string ssid;
  std::string password;
};

enum class NetworkWifiPowerSaveLevel { LOW_POWER, BALANCED, PERFORMANCE };

struct NetworkPolicy {
  NetworkWifiPowerSaveLevel initial_power_save = NetworkWifiPowerSaveLevel::BALANCED;
  // Append the OS fallback after saved user networks at runtime, without
  // persisting it or consuming a user slot. Fixed-network/radio-only policies
  // do not use the fallback even when this shared default is enabled.
  bool default_wifi_fallback_enabled = true;
  bool synchronize_clock_on_connect = true;
  NetworkJobPolicy jobs;
  // Optional product-owned bootstrap credentials. LunarOS seeds them only
  // when persistent storage contains no user credentials. General products,
  // including the desktop pet, leave this disabled.
  std::array<NetworkInitialWifiCredential,
             kNetworkInitialWifiCredentialCapacity>
      initial_wifi_credentials{};
  uint8_t initial_wifi_credential_count = 0;
  bool seed_initial_wifi_credentials_when_empty = false;
  // Force a fixed product network at boot, replacing previously saved Wi-Fi.
  // A matching saved list is left untouched to avoid repeated flash writes.
  bool force_initial_wifi_credentials = false;
  // False starts the same Network-owned radio without STA association. Such
  // products must also disable provisioning and clock synchronization.
  bool station_enabled = true;
  EspNowPolicy espnow;
};

} // namespace lunaredu::lunaros
