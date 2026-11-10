#include "app/execution/application_session.h"

namespace lunaredu::lunaros::apps {
SessionStorage session_validation_storage(const SessionStorage &owner,SessionStorage candidate){
  candidate.write=nullptr;
  candidate.cancelled=owner.cancelled;candidate.cancel_context=owner.cancel_context;
  candidate.observe=owner.observe;candidate.observation_context=owner.observation_context;
  candidate.device={};candidate.validation_only=true;return candidate;
}
bool SessionDeviceScope::bind(SessionDevice device,bool validation_only,std::string &error){
  if(bound_){error="application_device_resources_retained";return false;}
  device_=device;validation_only_=validation_only;bound_=true;
  revoked_=calling_=false;calls_=0;error.clear();return true;
}
void SessionDeviceScope::begin_callback(bool permits_effects){
  calls_=0;calling_=bound_&&!revoked_&&!validation_only_&&permits_effects;
}
void SessionDeviceScope::end_callback(){calling_=false;}
int32_t SessionDeviceScope::call(const ApplicationDeviceRequest &request,bool cancelled){
  // Saturation keeps hostile repeated imports from wrapping the budget.
  if(calls_<=LUNA_DEVICE_CALLBACK_BUDGET)++calls_;
  if(budget_exhausted())return LUNA_DEVICE_BUSY;
  if(!luna_device_request_valid(request.operation,request.a,request.b,request.c))return LUNA_DEVICE_INVALID;
  if(!calling_||cancelled||!device_.permitted||!device_.call)return LUNA_DEVICE_DENIED;
  return device_.call(device_.context,device_.instance,request);
}
void SessionDeviceScope::revoke(){
  calling_=false;
  if(!bound_||revoked_)return;
  revoked_=true;
  if(device_.revoke)device_.revoke(device_.context,device_.instance);
}
bool SessionDeviceScope::retire(std::string &error){
  revoke();
  if(bound_&&device_.retire&&!device_.retire(device_.context,device_.instance,error)){
    if(error.empty())error="application_device_retirement_pending";
    return false;
  }
  device_={};bound_=false;validation_only_=false;calls_=0;error.clear();return true;
}
}
