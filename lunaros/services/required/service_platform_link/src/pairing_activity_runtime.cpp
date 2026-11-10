#include "pairing_activity.h"
#include "pairing_session.h"
namespace lunaredu::lunaros {
namespace {
platform_link::PairingActivityOwner &activities() {
  static platform_link::PairingActivityOwner value({nullptr,
    [](void *) { return platform_link::observe_pairing_session(); },
    [](void *,bool refresh,uint64_t &session,bool &owned,std::string &error) { return platform_link::claim_pairing_session(refresh,session,owned,error); },
    [](void *,uint64_t session,std::string &error) { return platform_link::release_pairing_session(session,error); }});
  return value;
}
}
bool begin_platform_link_pairing_activity(const char *caller,PlatformLinkPairingActivityHandle &handle,std::string &error,bool refresh) { return activities().begin(caller,handle,error,refresh); }
bool read_platform_link_pairing_activity(const char *caller,PlatformLinkPairingActivityHandle handle,PlatformLinkPairingActivitySnapshot &out,std::string &error) { return activities().read(caller,handle,out,error); }
bool cancel_platform_link_pairing_activity(const char *caller,PlatformLinkPairingActivityHandle handle,std::string &error) { return activities().cancel(caller,handle,error); }
}
