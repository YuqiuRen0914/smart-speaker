#pragma once
#include <string>
namespace lunaredu::lunaros {
// Checked, ordered boot steps. After a failure no later step runs, and a nested
// startup operation may refine the borrowed stage without losing its diagnosis.
class StartupSequence {
 public:
  using Observer = void (*)(void *, const char *, const char *, const char *);
  StartupSequence(const char *&stage,std::string &error,Observer observer=nullptr,void *context=nullptr)
      :stage_(stage),error_(error),observer_(observer),context_(context) {}
  template<class Action> bool step(const char *stage,Action action,const char *reason="startup_failed") {
    if (!ready_) return false;
    stage_=stage; error_.clear();
    if (observer_) observer_(context_,stage_,"enter","");
    ready_=action(error_);
    if (!ready_ && error_.empty()) error_=reason;
    if (observer_) observer_(context_,stage_,ready_?"ready":"failed",error_.c_str());
    return ready_;
  }
  explicit operator bool() const { return ready_; }
 private:
  const char *&stage_; std::string &error_;
  bool ready_=true;
  Observer observer_=nullptr;
  void *context_=nullptr;
};
}
