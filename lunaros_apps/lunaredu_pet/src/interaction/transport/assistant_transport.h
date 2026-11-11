#pragma once

// Pet-owned LunarLink session transports. Wi-Fi lifecycle, clock and generic HTTP
// remain LunarOS services.
#include <string>

#include "lunaredu_lunaros/network/http_client.h"
#include "lunaredu_lunaros/network/network.h"
#include "interaction/transport/network_mqtt.h"
#include "interaction/transport/network_udp.h"
#include "interaction/transport/network_xiaozhi_mqtt_udp_channel.h"
#include "interaction/transport/network_xiaozhi_protocol.h"

inline bool assistant_network_connected() {
  return lunaredu::lunaros::network_link_state() ==
         lunaredu::lunaros::NetworkLinkState::Up;
}

inline bool assistant_network_provisioning_active() {
  return lunaredu::lunaros::network_provisioning_state() !=
         lunaredu::lunaros::NetworkProvisioningState::Idle;
}

inline bool assistant_synchronize_network_clock(uint32_t timeout_ms,
                                                std::string &error) {
  return lunaredu::lunaros::synchronize_network_clock_blocking(
      timeout_ms, error);
}
