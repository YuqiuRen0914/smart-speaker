#pragma once

#include <stdint.h>

namespace lunaredu::lunaros {

void start_captive_dns_server(uint32_t response_ip);
void stop_captive_dns_server();

} // namespace lunaredu::lunaros
bool captive_dns_server_running();
