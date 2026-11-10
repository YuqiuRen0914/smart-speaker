#pragma once
#include "lunaros_platform_link_pairing_activity.h"
#include <mutex>
namespace lunaredu::lunaros::platform_link {
struct PairingActivityObservation { PlatformLinkPairingPresentation state; uint64_t session = 0; bool pending = false; int64_t now_ms = 0; };
struct PairingActivityPorts {
  void *context;
  PairingActivityObservation (*observe)(void *);
  bool (*claim)(void *, bool refresh, uint64_t &session, bool &owned, std::string &);
  bool (*release)(void *, uint64_t session, std::string &);
};
class PairingActivityOwner {
public:
  explicit PairingActivityOwner(PairingActivityPorts, uint64_t initial_generation = 0);
  bool begin(const char *, PlatformLinkPairingActivityHandle &, std::string &, bool refresh = false);
  bool read(const char *, PlatformLinkPairingActivityHandle, PlatformLinkPairingActivitySnapshot &, std::string &);
  bool cancel(const char *, PlatformLinkPairingActivityHandle, std::string &);
private:
  void update();
  bool authorize(const char *, PlatformLinkPairingActivityHandle, std::string &) const;
  std::mutex mutex_;
  PairingActivityPorts ports_;
  PlatformLinkPairingActivitySnapshot snapshot_;
  std::string caller_;
  uint64_t generation_, session_ = 0;
  bool owned_ = false, cancelling_ = false;
};
}
