#pragma once

#include <stdint.h>

#include "lunaredu_lunaros/network/network_provisioning.h"

namespace lunaredu::lunaros {

bool provisioning_driver_station_suspended();
void provisioning_driver_set_station_suspended(bool suspended);
void provisioning_driver_disconnect_station();
bool provisioning_driver_configure_softap(uint32_t ip, uint32_t gateway,
                                            uint32_t subnet);
bool provisioning_driver_apply_softap_password_override(
    const std::string &password);
std::string provisioning_driver_softap_password();
bool provisioning_driver_start_softap(const char *ssid);
void provisioning_driver_stop_softap();
void provisioning_driver_start_captive_dns(uint32_t ip);
void provisioning_driver_stop_captive_dns();

} // namespace lunaredu::lunaros
