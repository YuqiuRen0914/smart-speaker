#pragma once
#include "app/execution/navigation_policy.h"
#include "desktop/application/launch.h"
#include "desktop/shell/interaction.h"
#include "shell/interaction_runtime.h"
namespace lunaredu::desktop::host_detail {
struct ShellInteractionInput {
  TouchInput touch;
  uint32_t now_ms=0;
  bool top_owned=false,blocked=false,system=false,confirm=false,back=false;
  int encoder=0,x=0,y=0,command=-1;
  uint64_t view_key=0;
  bool axes_fresh=true;
  bool contact_required=true;
};
struct ShellInteractionState { lunaredu::app::ApplicationNavigationRepeat &stick; bool &redraw; int &launcher_index; };
// Only tentative Shell changes roll back. App/resource retirement and input
// safety state belong to their owners and survive a rejected launch.
template<class Shell,class Route,class Start,class Invalidate>
ApplicationLaunchResult checked_application_launch(Shell &shell,const Route &page,
    Start start,Invalidate invalidate,std::string &error) {
  const auto previous_shell=shell;
  const auto previous_page=page;
  const auto result=start(error);
  if(result==ApplicationLaunchResult::Rejected&&page==previous_page) {
    shell=previous_shell;invalidate();
  }
  return result;
}
// All products feed logical controls. The Shell owns command/gesture ordering,
// menu Back, selection and launch/action dispatch; Apps own action semantics.
inline bool update_shell_interaction(DesktopShell &shell,lunaros::graphics::SceneRuntime &graphics,
    ShellInteractionState state,ShellInteractionInput input,ShellInteractionPort &port,bool changed) {
  if(input.command>=0)changed|=shell.command(static_cast<DesktopCommand>(input.command),true);
  const auto contact=input.contact_required
      ?update_desktop_contact(shell,graphics,input.view_key,input.touch,input.now_ms,
          input.top_owned,input.confirm||input.back||input.encoder||input.system,input.blocked)
      :[&]{
        // Page identity changes independently of contacts (for example when a
        // scan publishes results). Qualify it before the first future press.
        const auto before=graphics.page();
        const auto after=lunaros::graphics::select_desktop(graphics,input.view_key,shell);
        return DesktopContactResult{false,before.generation!=after.generation};
      }();
  changed|=contact.changed;state.redraw|=contact.generation_changed;
  port.widgets(input.confirm,input.back);changed|=state.redraw;
  if(input.back&&shell.surface==DesktopSurface::Launcher&&port.launcher_back_shortcut())changed=true;
  else {
    changed|=update_desktop_commands(shell,state.stick,
        {input.system,input.back,input.confirm,input.encoder,input.x,input.y,input.now_ms,input.axes_fresh});
    const int launched=shell.take_launch();state.launcher_index=static_cast<int>(shell.focus);
    if(launched>=0&&static_cast<size_t>(launched)<shell.count)changed|=port.launch(shell.apps[launched].id);
    port.action(shell.take_action());
  }
  if(!port.synchronize())return false;
  port.persist();changed|=port.poll(input.now_ms);return changed;
}
}
