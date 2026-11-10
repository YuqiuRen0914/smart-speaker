#include "presets/backends.h"
#include "runtime/preset.h"
namespace lunaredu::lunaros {
bool configure_execution_backends(const RuntimePreset &preset,std::string &error) {
  for(const auto *provider:preset.execution)
    if(provider->configure_backend && !provider->configure_backend(error))return false;
  error.clear(); return true;
}
} // namespace lunaredu::lunaros
