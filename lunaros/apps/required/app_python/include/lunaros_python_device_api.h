#pragma once
#include "device_api/projection_contribution.h"
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif
namespace lunaredu::lunaros { class DeviceProjectionRegistry; }
namespace lunaredu::python {
// Script/REPL/project operations belong to the system Python App. Registering
// their device projection makes the control surface available without a VM.
lunaros::ProductDeclarationNode projection_declaration_node(lunaros::DeviceProjectionRegistry &);
inline lunaros::DeviceProjectionSelection runtime_projection_selection() {
#if CONFIG_LUNAROS_ENABLE_MICROPYTHON
  static constexpr lunaros::DeviceProjectionDeclarationFactory entries[] = {projection_declaration_node};
  return {entries, 1};
#else
  return {};
#endif
}
}
