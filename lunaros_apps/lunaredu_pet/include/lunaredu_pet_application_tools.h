#pragma once
#include <string>
namespace lunaredu::pet {
struct ApplicationToolPolicy {
  void *context = nullptr;
  bool (*control_allowed)(void *) = nullptr;
};
// The sole application tool bridge; providers register with OS independently.
bool register_application_tools(const ApplicationToolPolicy &, std::string &error);
}
