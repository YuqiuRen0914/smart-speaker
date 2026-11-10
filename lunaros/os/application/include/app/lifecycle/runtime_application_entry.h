#pragma once
#include <memory>
#include <string>
#include <cstddef>
namespace lunaredu::lunaros { struct ApplicationContext; }
namespace lunaredu::app {
class AppController;
class RuntimeApplicationInstance {
 public:
  virtual ~RuntimeApplicationInstance() = default;
  virtual bool declare(std::string &) = 0;
};
struct RuntimeApplicationEntry {
  const char *id;
  std::unique_ptr<RuntimeApplicationInstance> (*create)(AppController &, lunaros::ApplicationContext &);
};
// Borrowed static descriptions. Owners are created only during declaration;
// the selected range must remain stable for the lifetime of its host.
struct RuntimeApplicationSelection {
  const RuntimeApplicationEntry *data = nullptr;
  size_t size = 0;
};
}
