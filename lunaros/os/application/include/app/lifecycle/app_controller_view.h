#pragma once
#include "app/lifecycle/application_state.h"
#include <string_view>
namespace lunaredu::app {
class AppController;
// Borrowed, allocation-free observer. Read only on the lifecycle owner, like
// controller mutation; this view supplies no cross-thread synchronization.
class AppControllerView {
public:
  explicit AppControllerView(const AppController &controller) : controller_(&controller) {}
  explicit AppControllerView(const AppController *controller) : controller_(controller) {}
  AppControllerProgress progress() const;
  bool has_applications() const;
  const char *active_application_id() const;
  bool foreground_current(std::string_view id) const;
private:
  const AppController *controller_;
};
}
