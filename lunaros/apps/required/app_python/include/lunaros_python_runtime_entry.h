#include "app/execution/application_context.h"
#pragma once
#include "app/lifecycle/runtime_application_entry.h"
#include "lunaros_python_application.h"
#include <new>
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif
namespace lunaredu::python {
class RuntimeApplicationOwner final : public app::RuntimeApplicationInstance {
 public:
  RuntimeApplicationOwner(app::AppController &controller, lunaros::ApplicationContext &device) : application_(controller, device) {}
  bool declare(std::string &error) override { return application_.declare(error); }
 private:
  SystemApplication application_;
};
inline app::RuntimeApplicationEntry runtime_entry() {
  return {kApplicationId, [](app::AppController &controller, lunaros::ApplicationContext &device) {
    return std::unique_ptr<app::RuntimeApplicationInstance>(new(std::nothrow) RuntimeApplicationOwner(controller, device));
  }};
}
}
namespace lunaredu::python {
inline app::RuntimeApplicationSelection runtime_selection() {
#if CONFIG_LUNAROS_ENABLE_MICROPYTHON
  static const app::RuntimeApplicationEntry entries[] = {runtime_entry()};
  return {entries, 1};
#else
  return {};
#endif
}
}
