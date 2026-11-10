#include "app/execution/pose_device.h"
#include <cmath>
namespace lunaredu::lunaros::apps {
bool ApplicationPoseDevice::available() const{
  return !blocked_.load(std::memory_order_acquire)&&ports_.available&&ports_.read&&ports_.now_ms&&ports_.available(ports_.context);
}
bool ApplicationPoseDevice::owns(const ApplicationDeviceIdentity &identity) const{
  return available()&&instance_&&identity.instance==instance_;
}
bool ApplicationPoseDevice::fresh(const ApplicationPoseSample &sample,uint32_t now) const{
  return sample.healthy&&uint32_t(now-sample.sampled_at_ms)<=ports_.maximum_age_ms&&
      uint32_t(now-sample.sampled_at_ms)<=uint32_t(now-started_ms_);
}
ApplicationDevicePorts ApplicationPoseDevice::ports(){
  return {this,LUNA_DEVICE_CAPABILITY(LUNA_DEVICE_POSE_MDEG),
    [](void *p,const ApplicationDeviceIdentity &identity,std::string &error){
      auto &s=*static_cast<ApplicationPoseDevice *>(p);
      if(!s.available()){error="pose_device_unavailable";return false;}
      if(!identity.instance||!identity.application_id||!*identity.application_id){error="pose_device_identity_invalid";return false;}
      if(s.instance_&&s.instance_!=identity.instance){error="pose_device_subscription_retained";return false;}
      if(!s.instance_)s.started_ms_=s.ports_.now_ms(s.ports_.context);
      s.instance_=identity.instance;error.clear();return true;
    },
    [](void *p,const ApplicationDeviceIdentity &identity){return static_cast<ApplicationPoseDevice *>(p)->owns(identity);},
    [](void *p,const ApplicationDeviceIdentity &identity)->uint32_t{
      auto &s=*static_cast<ApplicationPoseDevice *>(p);
      if(!s.owns(identity))return 0u;
      const auto sample=s.ports_.read(s.ports_.context);
      return s.fresh(sample,s.ports_.now_ms(s.ports_.context))?LUNA_DEVICE_CAPABILITY(LUNA_DEVICE_POSE_MDEG):0u;
    },
    [](void *p,const ApplicationDeviceIdentity &identity,const ApplicationDeviceRequest &request)->int32_t{
      auto &s=*static_cast<ApplicationPoseDevice *>(p);
      if(!s.owns(identity))return LUNA_DEVICE_REVOKED;
      if(request.operation!=LUNA_DEVICE_POSE_MDEG||!luna_device_request_valid(request.operation,request.a,request.b,request.c))return LUNA_DEVICE_UNSUPPORTED;
      const auto sample=s.ports_.read(s.ports_.context);
      if(!s.fresh(sample,s.ports_.now_ms(s.ports_.context)))return LUNA_DEVICE_STALE;
      const auto degrees=sample.degrees[size_t(request.a)];
      return std::isfinite(degrees)?int32_t(std::lround(std::remainder(double(degrees),360.0)*1000.0)):int32_t(LUNA_DEVICE_STALE);
    },
    [](void *p,const ApplicationDeviceIdentity &identity,std::string &error){
      auto &s=*static_cast<ApplicationPoseDevice *>(p);
      if(s.instance_&&s.instance_!=identity.instance){error="pose_device_subscription_owner_mismatch";return false;}
      s.instance_=0;s.started_ms_=0;error.clear();return true;
    },ports_.service_id};
}
}
