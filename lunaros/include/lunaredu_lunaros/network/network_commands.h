#pragma once

#include <cstdint>
#include <string>

#include "lunaredu_lunaros/network/network.h"

namespace lunaredu::lunaros {

// Typed commands crossing into the fixed NetworkService executor. These are
// internal service calls, not a wire protocol or an authorization boundary.
bool request_network_start_provisioning(NetworkProvisioningMode mode,
                                        bool suspend_station,
                                        std::string &error);
bool request_network_stop_provisioning(std::string &error);
bool request_network_apply_saved_credential(uint8_t index,
                                            std::string &error);
bool request_network_wifi_scan(std::string &error);
// HTTP/serial callers may wait for the shared background scan. Never call
// from a service executor or a network job runner. Timeout releases only the
// waiter; the job owns its state and may still finish for other observers.
bool request_network_wifi_scan_and_wait(NetworkWifiScanSnapshot &snapshot,
                                       std::string &error,
                                       uint32_t timeout_ms = 15000);

} // namespace lunaredu::lunaros
