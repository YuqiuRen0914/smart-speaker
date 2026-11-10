#include "modules/desktop_shell/semantic_state.h"
#include "modules/desktop_shell/presentation_builder.h"
using desktop_pet::DesktopSemanticState;
using desktop_pet::build_presentation_model;
using desktop_pet::reset_desktop_semantic_state;
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <new>
#include <optional>
#include <utility>
#include "modules/desktop_shell/presentation_builder.h"
#include "modules/desktop_shell/remote/presentation_snapshot.h"
#include "network/network_service_internal.h"
#include "device_link/device_link_state.h"

namespace { bool counting=false; size_t allocations=0, bytes=0; size_t observed=0; }
void *operator new(size_t n) {
  void *p=std::malloc(n ? n : 1); if(!p) throw std::bad_alloc();
  if(counting) { ++allocations; bytes+=n; } return p;
}
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
template<class F> void measure(const char *name,F run) {
  allocations=bytes=0; counting=true;
  for(unsigned i=0;i<100;++i) run();
  counting=false;
  std::printf("%s calls=100 allocations=%zu requested_bytes=%zu\n",name,allocations,bytes);
}
namespace {
lunaredu::lunaros::NetworkObservation observation;
bool start(void *,const lunaredu::lunaros::NetworkPolicy &,std::string &) {return true;}
bool stop(void *,std::string &) {return true;}
void poll(void *) {}
lunaredu::lunaros::NetworkObservation read(void *) { return observation; }
}
int main() {
  using namespace lunaredu::desktop;
using namespace desktop_pet::presentation;
  using namespace lunaredu::lunaros;
  DesktopSemanticState state;
  state.presentation_title=std::string(64,'t'); state.presentation_detail=std::string(96,'d');
  state.active_application_id=std::string(24,'a'); state.network_ssid=std::string(32,'s');
  state.launcher_item_count=12; state.file_entry_count=16; state.programming_script_count=8;
  for(auto &i:state.launcher_items) {i.application_id=std::string(24,'i'); i.title=std::string(48,'n'); i.icon=std::string(20,'g');}
  for(auto &i:state.file_entries) {i.name=std::string(40,'n'); i.path=std::string(80,'p'); i.detail=std::string(48,'d');}
  for(auto &i:state.programming_scripts) {i.name=std::string(40,'n'); i.path=std::string(80,'p'); i.detail=std::string(48,'d');}
  PresentationModel model;
  build_presentation_model(state,model);
  measure("presentation inactive lists rebuild",[&]{build_presentation_model(state,model);});
  assert(model.launcher_item_count==0 && model.file_entry_count==0 && model.programming_script_count==0);
  // Reading only common foreground fields must not allocate per cached item.
  assert(allocations <= 1000);
  publish_presentation_snapshot(std::move(model));
  measure("presentation read into fresh model",[&]{PresentationModel copy;uint32_t rev=0;read_presentation_snapshot(copy,rev); observed+=rev;});
  uint32_t rev=0; read_presentation_snapshot(model,rev);
  measure("presentation read reuse capacity",[&]{read_presentation_snapshot(model,rev); observed+=rev;});
  observation.ready=true; observation.credentials_available=true; observation.link=NetworkLinkState::Up;
  observation.target_ssid=std::string(32,'s'); observation.local_ip="192.168.100.100";
  observation.provisioning_ssid=std::string(24,'p');
  NetworkDriver driver; driver.start=start;driver.stop=stop;driver.poll=poll;driver.observe=read;
  std::string error; NetworkPolicy policy; policy.provisioning.enabled=false;
  network_service_internal().bind(driver,policy,error); network_service_internal().start(error);
  measure("network full snapshot to read link",[&]{observed+=network_snapshot().link==NetworkLinkState::Up;});
  measure("network link-only getter",[&]{observed+=network_link_state()==NetworkLinkState::Up;});
  assert(allocations==0);
  DeviceLinkPairingSnapshot pairing; pairing.state=DeviceLinkBindingState::Bound;
  pairing.account_id=std::string(36,'i');pairing.account_display_name=std::string(64,'n');
  pairing.account_avatar_url=std::string(128,'u');
  device_link::replace_pairing_snapshot(std::move(pairing));
  measure("pairing full snapshot to read state (three populated fields)",[&]{observed+=device_link::pairing_snapshot().state==DeviceLinkBindingState::Bound;});
  measure("pairing state-only getter",[&]{assert(device_link::pairing_state()==DeviceLinkBindingState::Bound);});
  assert(allocations==0);
  measure("DeviceLink network predicate",[&]{assert(device_link::network_connected());});
  assert(allocations==0);
  device_link::report_state();
  measure("steady bound report",[&]{device_link::report_state();});
  assert(allocations==0);
  assert(network_snapshot().device_link==NetworkDeviceLinkState::Ready);
  device_link::update_pairing_snapshot(DeviceLinkBindingState::Error, "", "", 0, "binding-error");
  device_link::report_state();
  assert(network_snapshot().device_link==NetworkDeviceLinkState::Degraded);
  assert(network_snapshot().last_error=="binding-error");
  device_link::update_pairing_snapshot(DeviceLinkBindingState::Pairing);
  device_link::report_state();
  assert(device_link::pairing_state()==DeviceLinkBindingState::Pairing);
  assert(network_snapshot().device_link==NetworkDeviceLinkState::Connecting);
  std::printf("host sizes Semantic=%zu Presentation=%zu Pairing=%zu Network=%zu observed=%zu\n",sizeof(state),sizeof(model),sizeof(DeviceLinkPairingSnapshot),sizeof(NetworkSnapshot),observed);
}

#include "lunaredu_lunaros/delivery/resources.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "kernel/system_primitives.h"
// Link-only substitutes for unrelated I/O paths; the measured getters do not call these.
namespace lunaredu::lunaros {
bool device_cloud_credential_ready(size_t) { return true; }
void set_private_resource_scope(uint32_t, const std::string &) {}
bool request_resource(ResourceDescriptor, int) { return false; }
namespace internal { uint32_t system_monotonic_ms() { return 1; } }
namespace device_link { void erase_cached_binding() {} }
}
