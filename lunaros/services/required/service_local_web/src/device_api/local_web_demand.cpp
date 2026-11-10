#include "device_api/local_web_activity.h"
#include "lunaros_local_web_lifecycle.h"
#include "lunaros_local_web_lifecycle.h"
#include "device_api/local_web_demand.h"
#include "device_api/http/standby_listener.h"
#include "lunaros_network.h"
#include "lunaros_network_jobs.h"
#include <atomic>
#include <esp_timer.h>
namespace lunaredu::lunaros {
namespace {
enum class Mode { Stopped, Standby, Full, Cleanup };
Mode mode=Mode::Stopped;
DeviceProjectionRegistry *owner=nullptr;
uint32_t idle_timeout=0;
std::atomic<uint32_t> requests{0}, last_request{0};
std::atomic<bool> transitioning{false};
std::atomic<bool> closing{false};
bool promote=false;
NetworkJobHandle transition_job;
uint32_t now_ms(){return uint32_t(esp_timer_get_time()/1000);}
NetworkJobRunResult transition(NetworkJobHandle,void *){
  std::string error;bool okay=true;
  if(promote){
    web_internal::standby_listener_stop();
    okay=web_internal::start_full(*owner,error);
    if(okay){mode=Mode::Full;last_request.store(now_ms());}
    else { mode=Mode::Cleanup;if(web_internal::stop_full(error)){mode=Mode::Stopped;if(web_internal::standby_listener_start(error))mode=Mode::Standby;} }
  }else if(mode==Mode::Full||mode==Mode::Cleanup){
    // Recheck on the worker; a request may have arrived since admission.
    if(mode==Mode::Full&&!closing&&(requests.load()||now_ms()-last_request.load()<idle_timeout||network_fast_snapshot().local_access!=NetworkLocalAccessState::Idle||network_maintenance_state()!=NetworkMaintenanceState::Normal)){
      transitioning.store(false,std::memory_order_release);return {true,false,"web_still_in_use"};
    }
    okay=web_internal::stop_full(error);
    if(okay){
      mode=Mode::Stopped;
      if(!closing){okay=web_internal::standby_listener_start(error);if(okay)mode=Mode::Standby;}
    }
  }
  transitioning.store(false,std::memory_order_release);
  return {okay,false,error};
}
bool schedule(bool full,std::string &error){
  promote=full;transitioning.store(true,std::memory_order_release);
  const NetworkJobDefinition job{.owner_service_id="device.api",.kind="local_web_transport",.priority=kNetworkPriorityInteractive,.runner=transition};
  if(submit_network_job(job,transition_job,error))return true;
  transitioning.store(false,std::memory_order_release);return false;
}
bool pending(){
  if(transition_job&&network_job_active(transition_job))return true;
  // A queued job can be cancelled/deadlined without invoking its runner.
  transitioning.store(false,std::memory_order_release);return false;
}
}
bool configure_local_web_idle_timeout(uint32_t timeout,std::string &error){
  if(owner||transitioning.load()){error="local_web_already_started";return false;}
  if(timeout&&timeout<1000){error="local_web_idle_timeout_too_short";return false;}
  idle_timeout=timeout;error.clear();return true;
}
namespace web_internal {
bool demand_enabled(){return idle_timeout!=0;}
bool demand_start(DeviceProjectionRegistry &registry,std::string &error){
  if(owner){error="device_api_already_started";return false;}
  if(!standby_listener_start(error))return false;
  owner=&registry;closing=false;mode=Mode::Standby;return true;
}
bool demand_stop(std::string &error){
  error.clear();if(!owner)return true;
  closing=true;
  if(pending()){error="local_web_transition_pending";return false;}
  if(mode==Mode::Full||mode==Mode::Cleanup){if(!schedule(false,error))return false;error="local_web_stop_pending";return false;}
  standby_listener_stop();mode=Mode::Stopped;owner=nullptr;return true;
}
}
void local_web_request_started(){requests.fetch_add(1);last_request.store(now_ms());}
void local_web_request_finished(){last_request.store(now_ms());requests.fetch_sub(1);}
void poll_local_web(){
  if(!owner||pending()||closing)return;
  const auto network=network_fast_snapshot();
  const bool provisioning=network.local_access!=NetworkLocalAccessState::Idle;
  std::string error;
  if(mode==Mode::Cleanup){(void)schedule(false,error);return;}
  if(mode==Mode::Stopped){if(web_internal::standby_listener_start(error))mode=Mode::Standby;return;}
  if(mode==Mode::Standby){
    if(network.local_access==NetworkLocalAccessState::Active||(!provisioning&&web_internal::standby_listener_poll(now_ms())))(void)schedule(true,error);
  }else if(!provisioning&&!requests.load()&&now_ms()-last_request.load()>=idle_timeout&&network_maintenance_state()==NetworkMaintenanceState::Normal){
    (void)schedule(false,error);
  }
}
}
