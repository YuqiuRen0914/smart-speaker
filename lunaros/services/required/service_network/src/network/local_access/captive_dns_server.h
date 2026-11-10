#pragma once

#include <cstdint>

namespace lunaredu::lunaros {
class KernelRuntime;
bool start_captive_dns_server(KernelRuntime &runtime, uint32_t response_ip);
// Nonblocking retirement step. Retain the network runtime and retry from the
// owner poll until the worker and its managed-task admission have both exited.
bool stop_captive_dns_server();
enum class CaptiveDnsState { Stopped, Preparing, Ready, Failed };
CaptiveDnsState captive_dns_server_state();
bool captive_dns_server_running();
} // namespace lunaredu::lunaros
