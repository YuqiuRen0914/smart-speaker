#include "app/execution/application_context.h"
#pragma once
#include <atomic>
#include <string>
#include "app/lifecycle/application.h"
namespace lunaredu::app { class AppController; }
namespace lunaredu::lunaros { struct DeviceApiContext; struct ApplicationContext; }
namespace lunaredu::python {
inline constexpr const char *kApplicationId = "system.python";
// App-owned boot action, called after foreground admission on the main owner.
bool run_boot_script(lunaros::ApplicationContext &, std::string &);
// Composition owns the instance; AppController borrows its lifecycle callbacks.
class SystemApplication {
public:
  SystemApplication(app::AppController &, lunaros::ApplicationContext &);
  app::ForegroundDefinition definition();
  bool declare(std::string &error);
  static bool enter(lunaros::DeviceApiContext &, std::string &);
  static bool leave(lunaros::DeviceApiContext &, std::string &);
  static bool leave_foreground(lunaros::ApplicationContext &, std::string &);
  static void accepted(lunaros::DeviceApiContext &, bool repl);
  static void rejected(lunaros::DeviceApiContext &);
  static bool foreground(const lunaros::DeviceApiContext &);
private:
  static SystemApplication *from(const lunaros::DeviceApiContext &);
  bool start(std::string &);
  bool stop(const char *, std::string &);
  void tick();
  bool restore(std::string &);
  app::AppController &applications_;
  lunaros::ApplicationContext &device_;
  std::atomic<bool> foreground_{false};
  bool transition_attempted_ = false;
  std::string return_to_;
  bool temporary_ = false, accepted_ = false, repl_ = false;
  bool boot_autorun_pending_ = false;
  bool vm_entry_pending_ = false;
  uint64_t expected_background_execution_ = 0;
};
}
