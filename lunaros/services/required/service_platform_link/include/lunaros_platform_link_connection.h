#pragma once

#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

enum class PlatformLinkConnectionPhase : uint8_t {
  Offline, Connecting, Connected, Failed, RetryWaiting, RetryQueued, Retrying,
};

enum class PlatformLinkConnectionFailure : uint8_t {
  None, Timeout, Security, Authentication, IdentityConflict, EndpointNotFound,
  RateLimited, ServiceUnavailable, RequestRejected, InvalidResponse, Transport,
};

struct PlatformLinkConnectionStatus {
  PlatformLinkConnectionPhase phase = PlatformLinkConnectionPhase::Connecting;
  PlatformLinkConnectionFailure failure = PlatformLinkConnectionFailure::None;
  int http_status = -1;
  uint32_t retry_wait_ms = 0;
};

PlatformLinkConnectionStatus platform_link_connection_status();
const char *platform_link_connection_phase_name(PlatformLinkConnectionPhase phase);
const char *platform_link_connection_failure_name(PlatformLinkConnectionFailure failure);
std::string platform_link_connection_status_text(const PlatformLinkConnectionStatus &status);

}
