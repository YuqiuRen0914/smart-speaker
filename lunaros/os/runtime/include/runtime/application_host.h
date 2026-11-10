#pragma once
#include <memory>
#include <string>
#include "app/lifecycle/runtime_application_entry.h"

namespace lunaredu::app { class AppController; }
namespace lunaredu::lunaros {
struct DeviceContext;
bool validate_runtime_applications(app::RuntimeApplicationSelection, std::string &error);

// One retained foreground owner. Selected runtime Apps are declared before the
// product registers/seals its capabilities. No task or VM is started by ensure.
// poll runs on the existing main executor, including failed-start cleanup.
class ApplicationHost {
public:
  ApplicationHost();
  ~ApplicationHost();
  ApplicationHost(const ApplicationHost &) = delete;
  ApplicationHost &operator=(const ApplicationHost &) = delete;
  bool ensure(DeviceContext &, app::RuntimeApplicationSelection, std::string &error);
  bool reset(std::string &error); // Checked boot-only declaration retirement.
  app::AppController *get() const;
  app::AppController *operator->() const { return get(); }
  bool poll(const char *initial_application, bool start_allowed,
            std::string &error);
  // Current Running/Idle observation, not a latched successful boot.
  bool ready() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
