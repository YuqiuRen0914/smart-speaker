#pragma once
#include "app/execution/application_device.h"
#include <array>
#include <atomic>
namespace lunaredu::lunaros::apps {
// Cached observation supplied by a device adapter, never by the guest.
struct ApplicationPoseSample {
  std::array<float,3> degrees{};
  uint32_t sampled_at_ms=0;
  bool healthy=false;
};
struct ApplicationPosePorts {
  void *context=nullptr;
  bool (*available)(void *)=nullptr;
  ApplicationPoseSample (*read)(void *)=nullptr;
  uint32_t (*now_ms)(void *)=nullptr;
  const char *service_id=nullptr;
  uint32_t maximum_age_ms=100;
};
// Main-owned read-only subscription. Device owners merge subscribed() with
// their other consumers; retirement never stops another consumer's sampler.
class ApplicationPoseDevice {
 public:
  explicit ApplicationPoseDevice(ApplicationPosePorts ports):ports_(ports){}
  ApplicationDevicePorts ports();
  void block(bool value){blocked_.store(value,std::memory_order_release);}
  bool subscribed() const{return instance_!=0;}
  bool available() const;
 private:
  bool owns(const ApplicationDeviceIdentity &) const;
  bool fresh(const ApplicationPoseSample &,uint32_t) const;
  ApplicationPosePorts ports_;
  std::atomic<bool> blocked_{false};
  uint64_t instance_=0;
  uint32_t started_ms_=0;
};
}
