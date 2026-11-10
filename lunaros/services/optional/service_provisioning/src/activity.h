#pragma once
#include "lunaros_provisioning_activity.h"
#include <mutex>
namespace lunaredu::lunaros::provisioning {
struct ActivityObservation { ProvisioningSnapshot state; std::string url, password; };
struct ActivityPorts {
  void *context;
  ActivityObservation (*observe)(void *);
  bool (*claim)(void *, uint64_t &session, bool &owned, std::string &);
  bool (*release)(void *, uint64_t session, std::string &);
};
class ActivityOwner {
public:
  explicit ActivityOwner(ActivityPorts, uint64_t initial_generation = 0);
  bool begin(const char *, ProvisioningActivityHandle &, std::string &);
  bool read(const char *, ProvisioningActivityHandle, ProvisioningActivitySnapshot &, std::string &);
  bool cancel(const char *, ProvisioningActivityHandle, std::string &);
private:
  void update();
  bool authorize(const char *, ProvisioningActivityHandle, std::string &) const;
  std::mutex mutex_;
  ActivityPorts ports_;
  ProvisioningActivitySnapshot snapshot_;
  std::string caller_;
  uint64_t generation_, session_ = 0;
  uint32_t initial_attempt_ = 0;
  bool owned_ = false, cancelling_ = false;
};
}
