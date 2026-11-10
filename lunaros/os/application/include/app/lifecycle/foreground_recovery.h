#pragma once
#include <cstdint>
#include <string>

namespace lunaredu::app {
class AppController;
// Main-owner-local intent for checked maintenance retirement. This object
// neither suspends services nor authorizes outputs. Keep it alive across failed
// stops/restores; the controller and registered Apps remain their sole owners.
class ForegroundRecovery {
public:
  bool pending() const {return captured_;}
  ForegroundRecovery()=default;
  ForegroundRecovery(const ForegroundRecovery &)=delete;
  ForegroundRecovery &operator=(const ForegroundRecovery &)=delete;
  // False retains intent for retry. Lifecycle pending errors are neutral:
  // foreground_cleanup_pending, foreground_transition_pending and
  // foreground_restore_pending. Protocol naming belongs to each caller.
  bool quiesce(AppController *,const char *reason,const char *fallback,std::string &error);
  bool restore(AppController *,std::string &error);
private:
  void clear(){destination_.clear();owner_=nullptr;captured_=false;expected_transition_=0;}
  AppController *owner_=nullptr;
  std::string destination_;
  uint32_t expected_transition_=0;
  bool captured_=false;
};
}
