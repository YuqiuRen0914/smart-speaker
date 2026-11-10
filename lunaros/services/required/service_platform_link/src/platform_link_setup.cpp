#include "lunaros_platform_link_setup.h"
#include <sys/time.h>

namespace lunaredu::lunaros {
PlatformLinkSetupBinding platform_link_setup_binding(const PlatformLinkSetupPolicy &policy,bool begin,int64_t now_ms) {
  if(!policy.browser_origin||!*policy.browser_origin||platform_link_binding_browser_origin()!=policy.browser_origin)
    return {"error",{},"binding_environment_unsupported"};
  const auto current=platform_link_pairing_snapshot();
  if(current.state==PlatformLinkBindingState::Bound)return {"bound",{}, {}};
  if(current.state==PlatformLinkBindingState::Pairing&&!current.code.empty()) {
    if(current.expires_at_ms>now_ms+policy.minimum_lifetime_ms)return {"ready",current.code,{},current.expires_at_ms};
    if(begin)(void)refresh_platform_link_pairing();
    return {};
  }
  if(begin&&request_platform_link_pairing())return {};
  if(current.state==PlatformLinkBindingState::Error)return {"error",{},current.error};
  return {};
}
PlatformLinkSetupBinding platform_link_setup_binding(const PlatformLinkSetupPolicy &policy,bool begin) {timeval now{};gettimeofday(&now,nullptr);return platform_link_setup_binding(policy,begin,int64_t(now.tv_sec)*1000+now.tv_usec/1000);}
}
