#include "app/execution/application_device.h"
#include <array>
#include <algorithm>
#include <mutex>
#include <limits>
#include <cstdlib>
#include <chrono>

namespace lunaredu::lunaros::apps {
namespace {
constexpr uint32_t known_mask=LUNA_DEVICE_KNOWN_CAPABILITIES;
size_t sample_slot(int32_t operation,int32_t channel) {
  size_t offset=0;
  for(const auto &contract:application_device_contracts()) {
    if(contract.operation==operation)return channel>=0&&channel<contract.read_channels?offset+size_t(channel):LUNA_DEVICE_SAMPLE_COUNT;
    offset+=contract.read_channels;
  }
  return LUNA_DEVICE_SAMPLE_COUNT;
}
}
const ApplicationDeviceContract *application_device_contract(int32_t operation){
  for(const auto &contract:application_device_contracts())if(contract.operation==operation)return &contract;
  return nullptr;
}
bool application_device_value_valid(int32_t operation,int32_t value){
  const auto *contract=application_device_contract(operation);
  return contract&&!contract->writes_output&&value>=contract->minimum&&value<=contract->maximum;
}
ApplicationDeviceOutcome application_device_outcome(const ApplicationDeviceRequest &request,int32_t result) {
  const auto *contract=application_device_contract(request.operation);
  if(LUNA_DEVICE_VALUE_VALID(result))return {contract&&contract->writes_output?
    kApplicationDeviceWriteAcknowledgement:kApplicationDeviceReadAcknowledgement,OperationError::None};
  switch(result) {
  case LUNA_DEVICE_INVALID:return {OperationCompletion::NotStarted,OperationError::InvalidArgument};
  case LUNA_DEVICE_UNSUPPORTED:return {OperationCompletion::NotStarted,OperationError::NotFound};
  case LUNA_DEVICE_DENIED:case LUNA_DEVICE_REVOKED:return {OperationCompletion::NotStarted,OperationError::Forbidden};
  case LUNA_DEVICE_BUSY:return {OperationCompletion::NotStarted,OperationError::Conflict};
  case LUNA_DEVICE_STALE:return {OperationCompletion::NotStarted,OperationError::Unavailable};
  default:return {OperationCompletion::Unknown,OperationError::Internal};
  }
}
struct ApplicationDeviceHost::Impl {
  mutable std::mutex mutex;
  ApplicationDevicePorts ports;
  std::string id,error;
  uint64_t generation=0;
  bool configured=false,live=false,permission=false,revoked=false,retired=true,provider_owned=false;
  uint32_t available=0;
  uint32_t updated_at=0;
  int32_t last_result=0;
  ApplicationDeviceOutcome last_outcome{OperationCompletion::NotStarted,OperationError::None};
  std::array<int32_t,LUNA_DEVICE_SAMPLE_COUNT> samples{};
  std::array<ApplicationDeviceRequest,8> queue{};
  std::array<uint32_t,8> queued_at{};
  size_t read=0,count=0;
  uint32_t now_ms() const {
    return ports.monotonic_ms?ports.monotonic_ms(ports.clock_context):uint32_t(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  ApplicationDeviceIdentity identity(){return {id.c_str(),generation};}
  static int32_t call(void *opaque,uint64_t instance,const ApplicationDeviceRequest &request){
    auto &s=*static_cast<Impl*>(opaque);std::lock_guard lock(s.mutex);
    if(instance!=s.generation||!s.live||s.revoked)return LUNA_DEVICE_REVOKED;
    if(!s.permission)return LUNA_DEVICE_DENIED;
    if(!luna_device_request_valid(request.operation,request.a,request.b,request.c))return LUNA_DEVICE_INVALID;
    if(request.operation==LUNA_DEVICE_QUERY){
      switch(request.a){case LUNA_DEVICE_CAPABILITIES:return uint32_t(s.now_ms()-s.updated_at)<=LUNA_DEVICE_CACHE_MS?int32_t(s.available):0;
      case LUNA_DEVICE_CONTRACT:return LUNA_DEVICE_CONTRACT_VERSION;
      case LUNA_DEVICE_LAST_RESULT:return s.last_result;
      case LUNA_DEVICE_PENDING:return int32_t(s.count);}
    }
    if(!(s.ports.capabilities&LUNA_DEVICE_CAPABILITY(request.operation)))return LUNA_DEVICE_UNSUPPORTED;
    if(uint32_t(s.now_ms()-s.updated_at)>LUNA_DEVICE_CACHE_MS)return LUNA_DEVICE_STALE;
    if(!(s.available&LUNA_DEVICE_CAPABILITY(request.operation)))return LUNA_DEVICE_STALE;
    const auto *contract=application_device_contract(request.operation);
    if(!contract->writes_output){
      const size_t index=sample_slot(request.operation,request.a);
      return index<s.samples.size()?s.samples[index]:LUNA_DEVICE_INVALID;
    }
    if(s.count==s.queue.size())return LUNA_DEVICE_BUSY;
    const auto slot=(s.read+s.count)%s.queue.size();s.queue[slot]=request;s.queued_at[slot]=s.now_ms();++s.count;
    s.last_outcome=application_device_outcome(request,LUNA_DEVICE_OK);
    return LUNA_DEVICE_OK; // Accepted, physical completion is Main's result.
  }
  static void revoke(void *opaque,uint64_t instance){
    auto &s=*static_cast<Impl*>(opaque);std::lock_guard lock(s.mutex);
    if(instance!=s.generation)return;
    s.revoked=true;s.count=0;s.available=0;
  }
  static bool retire(void *opaque,uint64_t instance,std::string &error){
    auto &s=*static_cast<Impl*>(opaque);std::lock_guard lock(s.mutex);
    if(instance!=s.generation){error="application_device_instance_stale";return false;}
    s.revoked=true;s.count=0;s.available=0;
    if(!s.retired){error="application_device_retirement_pending";return false;}
    error.clear();return true;
  }
};
ApplicationDeviceHost::ApplicationDeviceHost():impl_(new Impl){}
ApplicationDeviceHost::~ApplicationDeviceHost(){
  // The provider can still retain this identity after a failed stop. A live
  // composition must retire before destruction, just like AppController.
  if(!retired())std::abort();
}
bool ApplicationDeviceHost::configure(const ApplicationDevicePorts &ports,std::string &error){
  auto &s=*impl_;std::lock_guard lock(s.mutex);
  if(s.configured){
    if(s.ports.context==ports.context&&s.ports.capabilities==ports.capabilities&&s.ports.admit==ports.admit&&
      s.ports.authorized==ports.authorized&&s.ports.available==ports.available&&s.ports.invoke==ports.invoke&&s.ports.retire==ports.retire&&s.ports.provider_service_id==ports.provider_service_id&&
      s.ports.monotonic_ms==ports.monotonic_ms&&s.ports.clock_context==ports.clock_context){error.clear();return true;}
    error="application_device_ports_immutable";return false;
  }
  if(!s.retired||(ports.capabilities&~known_mask)||(ports.provider_service_id&&!*ports.provider_service_id)||(ports.capabilities&&
      (!ports.provider_service_id||!*ports.provider_service_id||!ports.admit||!ports.authorized||!ports.available||!ports.invoke||!ports.retire))){error="application_device_ports_invalid";return false;}
  s.ports=ports;s.configured=true;error.clear();return true;
}
bool ApplicationDeviceHost::begin(const std::string &id,bool permission,std::string &error){
  auto &s=*impl_;
  {
    std::lock_guard lock(s.mutex);
    if(!s.retired){error="application_device_resources_retained";return false;}
    if(s.generation==std::numeric_limits<uint64_t>::max()){error="application_device_generation_exhausted";return false;}
    ++s.generation;s.id=id;s.permission=permission;s.revoked=false;s.live=true;s.retired=false;s.available=0;
    s.count=s.read=0;s.last_result=0;s.error.clear();s.samples.fill(LUNA_DEVICE_STALE);
    s.last_outcome={OperationCompletion::NotStarted,OperationError::None};
    s.provider_owned=permission&&s.ports.capabilities;
  }
  // Own even a rejected partial provider admission until retire confirms it.
  if(s.provider_owned&&!s.ports.admit(s.ports.context,s.identity(),error)){
    std::lock_guard lock(s.mutex);s.revoked=true;s.error=error;return false;
  }
  poll();error.clear();return true;
}
SessionDevice ApplicationDeviceHost::binding(bool permission){
  auto &s=*impl_;std::lock_guard lock(s.mutex);
  return {&s,s.generation,permission&&s.permission,Impl::call,Impl::revoke,Impl::retire};
}
void ApplicationDeviceHost::revoke(){auto &s=*impl_;Impl::revoke(&s,s.generation);}
bool ApplicationDeviceHost::retired() const {auto &s=*impl_;std::lock_guard lock(s.mutex);return s.retired;}
const std::string &ApplicationDeviceHost::failure() const {return impl_->error;}
const char *ApplicationDeviceHost::provider_service_id() const {return impl_->ports.provider_service_id;}
ApplicationDeviceStatus ApplicationDeviceHost::status() const {
  auto &s=*impl_;std::lock_guard lock(s.mutex);
  const auto available=s.live&&!s.revoked&&uint32_t(s.now_ms()-s.updated_at)<=LUNA_DEVICE_CACHE_MS?s.available:0;
  return {LUNA_DEVICE_CONTRACT_VERSION,s.ports.capabilities,available,uint32_t(s.count),
    s.generation,s.last_result,s.live&&s.permission&&!s.revoked,s.revoked,s.retired,
    s.last_outcome.completion,s.last_outcome.category};
}
void ApplicationDeviceHost::poll(){
  auto &s=*impl_;bool retiring=false,owned=false;ApplicationDeviceIdentity identity;ApplicationDevicePorts ports;
  {
    std::lock_guard lock(s.mutex);if(!s.live||s.retired)return;
    retiring=s.revoked;owned=s.provider_owned;identity=s.identity();ports=s.ports;
  }
  if(!retiring&&owned&&!ports.authorized(ports.context,identity)){revoke();retiring=true;}
  if(retiring){
    std::string error;const bool stopped=!owned||ports.retire(ports.context,identity,error);
    std::lock_guard lock(s.mutex);s.error=error;
    if(stopped){s.retired=true;s.live=false;s.provider_owned=false;s.count=0;s.available=0;}
    return;
  }
  const uint32_t available=owned?ports.available(ports.context,identity)&ports.capabilities:0;
  std::array<int32_t,LUNA_DEVICE_SAMPLE_COUNT> samples;samples.fill(LUNA_DEVICE_STALE);
  size_t slot=0;
  for(const auto &contract:application_device_contracts()) {
    for(uint8_t channel=0;channel<contract.read_channels;++channel,++slot) {
      if(available&LUNA_DEVICE_CAPABILITY(contract.operation)){
        const auto value=ports.invoke(ports.context,identity,{contract.operation,channel,0,0});
        samples[slot]=application_device_value_valid(contract.operation,value)?value:LUNA_DEVICE_STALE;
      }
    }
  }
  {std::lock_guard lock(s.mutex);if(!s.revoked){s.available=available;s.samples=samples;s.updated_at=s.now_ms();}}
  for(unsigned budget=0;budget<4;++budget){
    ApplicationDeviceRequest request;uint32_t queued_at=0;
    {std::lock_guard lock(s.mutex);if(s.revoked||!s.count)break;request=s.queue[s.read];queued_at=s.queued_at[s.read];s.read=(s.read+1)%s.queue.size();--s.count;}
    // No provider callback runs under the host lock. Main is the sole physical
    // dispatcher; revoke discards pending commands and retirement drains any
    // already accepted provider work before acknowledging release.
    const auto result=uint32_t(s.now_ms()-queued_at)<=LUNA_DEVICE_CACHE_MS&&ports.authorized(ports.context,identity)&&
      (ports.available(ports.context,identity)&LUNA_DEVICE_CAPABILITY(request.operation))?
      ports.invoke(ports.context,identity,request):LUNA_DEVICE_REVOKED;
    {std::lock_guard lock(s.mutex);s.last_result=result;s.last_outcome=application_device_outcome(request,result);}
    if(result!=LUNA_DEVICE_OK){revoke();break;}
  }
}
}
