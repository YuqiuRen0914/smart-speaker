#pragma once
#include <cstring>
#include <string>
namespace lunaredu::lunaros {
// Startup is a one-shot desktop action, never a shortcut around Home. A user
// transition cancels it; failed/removed destinations stay on the desktop.
class DesktopStartupApplication {
 public:
  template<class Switch> bool poll(bool animation_done, bool desktop_presented,
      const char *active, const char *shell, const char *destination,
      Switch switch_to, std::string &error) {
    if(completed_)return true;
    if(!active||!shell)return false;
    if(std::strcmp(active,shell)){completed_=true;return true;}
    if(!animation_done||!desktop_presented)return false;
    completed_=true;
    if(destination&&*destination&&std::strcmp(destination,shell))
      (void)switch_to(destination,error);
    return true;
  }
  bool completed() const {return completed_;}
 private:
  bool completed_=false;
};
}
