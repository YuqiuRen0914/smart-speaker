#pragma once
#include "desktop/ui/host.h"
#include "desktop/composition/applications.h"
#include <array>
namespace lunaredu::lunaros {
class DesktopShellObservers final : public desktop::ShellObservers {
 public:
  bool configure(KernelRuntime &,const DesktopAppDescriptor *,size_t,
      const desktop::DesktopApplication *,size_t,std::string &);
  bool poll(desktop::DesktopShell &,uint32_t,bool visible) override;
 private:
  std::array<const DesktopAppDescriptor *,desktop::DesktopShell::capacity> providers_{};
  size_t count_=0;
};
}
