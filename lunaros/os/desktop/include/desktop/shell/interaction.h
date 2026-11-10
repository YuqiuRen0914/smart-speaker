#pragma once
#include <cstdint>
namespace lunaredu::desktop {
class ShellInteractionPort {
 public:
  virtual ~ShellInteractionPort()=default;
  virtual void widgets(bool &confirm,bool &back)=0;
  virtual bool launcher_back_shortcut()=0;
  virtual bool launch(const char *)=0;
  virtual void action(const char *)=0;
  virtual bool synchronize()=0;
  virtual void persist()=0;
  virtual bool poll(uint32_t)=0;
};
}
