#pragma once
#include "desktop/navigation/runtime.h"
#include "desktop/ui/touch_router.h"
#include "desktop/ui/system_views.h"
namespace lunaredu::desktop {
struct ShellNavigationState {
  DesktopShell &shell;
  TouchInputRouter &touch;
  app::ViewRoute &route;
  bool &wait_release;
  bool &redraw;
};
class ShellNavigationPort {
 public:
  virtual ~ShellNavigationPort() = default;
  virtual void cancel_widgets() = 0;
  virtual void cancel_application_input() = 0;
  virtual bool leave_for_home(std::string &) = 0;
  virtual void suspend_observers(uint32_t) = 0;
  virtual bool sync_foreground() = 0;
  virtual void show_shell() = 0;
  // Business cancellation/root handling is App-owned. false delivers one
  // logical Back to the App input handler; no physical key is synthesized.
  virtual bool back_application() = 0;
  virtual void paint(NavigationPaint) = 0;
  virtual void rejected(const std::string &) = 0;
};
// OS owns gesture completion, Shell return, contact retirement and checked
// Home ordering. Products supply logical input and registered App hooks only.
class ShellNavigation final : public NavigationLifecycle {
 public:
  ShellNavigation(ShellNavigationState state, ShellNavigationPort &port) : state_(state),port_(port) {}
  NavigationResult update(const NavigationInput &input) {
    now_=input.now_ms; return update_navigation(state_.shell,input,*this);
  }
  void cancel_input(NavigationInputOwner owner) override {
    port_.cancel_widgets(); state_.touch.cancel();
    if (owner==NavigationInputOwner::Home) state_.shell.cancel_contact();
    else {
      state_.shell.contact=false; state_.shell.dragged=false;
      if (owner==NavigationInputOwner::Edge) { state_.shell.blocked_contact=true;state_.wait_release=true; }
    }
    if (owner!=NavigationInputOwner::Edge)port_.cancel_application_input();
  }
  bool leave_for_home(std::string &error) override {
    if(!port_.leave_for_home(error))return false;
    port_.suspend_observers(now_);state_.wait_release=true;return true;
  }
  void exit_rejected(const std::string &error) override { state_.redraw=true;port_.rejected(error); }
  void home_finished() override { state_.shell.command(DesktopCommand::Home,true);port_.show_shell(); }
  bool back_finished() override {
    state_.wait_release=true;state_.touch.cancel();
    if(state_.route!=kShellView)return port_.back_application();
    state_.shell.command(DesktopCommand::Back,true);
    if(!port_.sync_foreground())return true;
    port_.paint(NavigationPaint::Menu);return true;
  }
  void render(NavigationPaint paint) override { port_.paint(paint); }
 private:
  ShellNavigationState state_;
  ShellNavigationPort &port_;
  uint32_t now_=0;
};
}
