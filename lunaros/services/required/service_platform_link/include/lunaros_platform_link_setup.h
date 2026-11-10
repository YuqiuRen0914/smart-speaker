#pragma once
#include "lunaros_platform_link.h"
namespace lunaredu::lunaros {
struct PlatformLinkSetupBinding {std::string state="pending",code,error;int64_t expires_at_ms=0;};
// Product retains account/environment admission and minimum handoff lifetime.
// This adapter only observes state and queues work; it never blocks for cloud IO.
struct PlatformLinkSetupPolicy {
  const char *browser_origin;
  uint32_t minimum_lifetime_ms=120000;
};
PlatformLinkSetupBinding platform_link_setup_binding(const PlatformLinkSetupPolicy &policy,bool begin,int64_t now_ms);
PlatformLinkSetupBinding platform_link_setup_binding(const PlatformLinkSetupPolicy &policy,bool begin);
template<auto &Policy> PlatformLinkSetupBinding platform_link_setup_binding(bool begin) {
  return platform_link_setup_binding(Policy,begin);
}
}
