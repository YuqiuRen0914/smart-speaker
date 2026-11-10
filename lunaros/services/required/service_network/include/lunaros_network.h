#pragma once
#include "lunaros_network_service_id.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

inline constexpr uint32_t kNetworkPollIntervalMs = 20;
inline constexpr size_t kNetworkWifiEventQueueCapacity = 16;
// Boot-lifetime saturating counters. Clearing a driver queue is distinct from
// overflow and processing; no event payloads or credentials are retained.
struct NetworkWifiEventMetrics {
  uint32_t queued = 0, high_water = 0, enqueued = 0, processed = 0;
  uint32_t overflow_dropped = 0, cleared = 0;
  bool operator!=(const NetworkWifiEventMetrics &other) const {
    return queued != other.queued || high_water != other.high_water ||
        enqueued != other.enqueued || processed != other.processed ||
        overflow_dropped != other.overflow_dropped || cleared != other.cleared;
  }
  bool operator==(const NetworkWifiEventMetrics &other) const { return !(*this != other); }
};

enum class NetworkLinkState : uint8_t {
  Down,
  Connecting,
  Up,
};

enum class NetworkClockState : uint8_t {
  Unsynchronized,
  Synchronizing,
  Valid,
};

enum class NetworkLocalAccessState : uint8_t { Idle, Active, Preparing, Stopping };

// Safe physical facts, independently owned by each copied value. The driver
// fills this contract directly; publication never copies lifecycle metadata.
struct NetworkPhysicalState {
  NetworkWifiEventMetrics wifi_events;
  bool platform_ready = false;
  bool credentials_available = false;
  NetworkLinkState link = NetworkLinkState::Down;
  int32_t rssi = 0;
  int status_code = 0;
  int last_disconnect_reason = 0;
  int channel = 0;
  bool station_suspended = false;
  bool softap_active = false;
  bool bluetooth_available = false, bluetooth_enabled = false, bluetooth_connected = false;
  uint32_t reconnect_attempts = 0;
  uint32_t disconnect_started_at_ms = 0;
  uint32_t next_reconnect_delay_ms = 0;
  NetworkLocalAccessState local_access = NetworkLocalAccessState::Idle;
  NetworkClockState clock = NetworkClockState::Unsynchronized;
  std::string local_ip;
  std::string target_ssid;
  std::string local_access_ssid;
  std::string local_access_ip;
};

struct NetworkSnapshot : NetworkPhysicalState {
  uint32_t sequence = 0;
  bool driver_bound = false;
  bool service_started = false;
  std::string last_error;
  bool fixed_wifi_credentials = false;
};

struct NetworkFastSnapshot {
  bool retirement_pending = false; // New work is denied; cleanup remains admitted.
  NetworkClockState clock = NetworkClockState::Unsynchronized;
  bool fixed_wifi_credentials = false;
  uint8_t channel = 0;
  uint32_t sequence = 0;
  bool service_started = false;
  bool station_enabled = true;
  bool credentials_available = false;
  NetworkLinkState link = NetworkLinkState::Down;
  NetworkLocalAccessState local_access = NetworkLocalAccessState::Idle;
  bool platform_ready = false;
  bool local_access_has_address = false;
  bool station_has_address = false;
  bool local_access_surface_active = false;
  bool bluetooth_available = false;
  bool bluetooth_enabled = false;
  bool bluetooth_connected = false;
};

// Safe network facts. Driver callbacks, lifecycle control, locks and
// credentials are deliberately absent from the public snapshot.
// Wi-Fi, provisioning and the network job queue share one physical device.
// Recomposition is allowed only after its service and jobs have stopped.

NetworkSnapshot network_snapshot();
void network_snapshot_into(NetworkSnapshot &snapshot);
// Allocation-free fast-path for realtime tasks.  The full snapshot owns
// strings and must not be copied from audio/render loops.
NetworkFastSnapshot network_fast_snapshot();
} // namespace lunaredu::lunaros
