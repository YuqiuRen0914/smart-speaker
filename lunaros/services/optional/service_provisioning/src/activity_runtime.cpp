#include "activity.h"
#include "activity_session.h"
namespace lunaredu::lunaros {
namespace {
provisioning::ActivityOwner &activities() {
  static provisioning::ActivityOwner value({nullptr,
    [](void *) { return provisioning::observe_session(); },
    [](void *,uint64_t &session,bool &owned,std::string &error) { return provisioning::claim_session(session,owned,error); },
    [](void *,uint64_t session,std::string &error) { return provisioning::release_session(session,error); }});
  return value;
}
}
bool begin_provisioning_activity(const char *caller,ProvisioningActivityHandle &handle,std::string &error) { return activities().begin(caller,handle,error); }
bool read_provisioning_activity(const char *caller,ProvisioningActivityHandle handle,ProvisioningActivitySnapshot &out,std::string &error) { return activities().read(caller,handle,out,error); }
bool cancel_provisioning_activity(const char *caller,ProvisioningActivityHandle handle,std::string &error) { return activities().cancel(caller,handle,error); }
}
