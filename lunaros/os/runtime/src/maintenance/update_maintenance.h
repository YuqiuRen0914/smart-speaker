#pragma once
#include "runtime/update_maintenance.h"
#include "app/lifecycle/foreground_recovery.h"
#include "kernel/service.h"
#include <array>
#include <atomic>
#include <mutex>
namespace lunaredu::lunaros {
class RuntimeUpdateMaintenance {
 public:
  explicit RuntimeUpdateMaintenance(DeviceContext &device):device_(device){}
  bool configure(UpdateMaintenanceDefinition,std::string &);
  void poll(const char *executor);
  bool active() const {return phase_.load(std::memory_order_acquire)!=0;}
  bool blocked() const {return active()&&!admission_released_.load(std::memory_order_acquire);}
  bool quiesced() const {return phase_.load(std::memory_order_acquire)==2;}
  uint32_t epoch() const {return active()?epoch_.load(std::memory_order_acquire):0;}
  bool request(bool entering,std::string &error);
 private:
  struct Participant {
    std::atomic<uint32_t> entered{0},released{0},revoked{0};
    bool touched=false;
    std::string error;
  };
  bool all(bool entering,uint32_t epoch) const;
  DeviceContext &device_;
  UpdateMaintenanceDefinition definition_{};
  std::array<Participant,16> participants_{};
  std::atomic<uint32_t> epoch_{0};
  std::atomic<unsigned> phase_{0}; // idle / retiring / quiesced / restoring
  std::atomic<bool> main_quiesced_{false};
  std::mutex request_mutex_;
  app::ForegroundRecovery foreground_;
  ServiceResourceWindow background_;
  bool configured_=false;
  std::atomic<bool> admission_released_{false};
};
}
