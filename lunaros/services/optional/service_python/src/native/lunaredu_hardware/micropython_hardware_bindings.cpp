#include "lunaros_micropython_hardware_bindings.h"

#include <sdkconfig.h>

#ifndef CONFIG_LUNAREDU_MICROPYTHON_ENABLE_HARDWARE_BINDINGS
#define CONFIG_LUNAREDU_MICROPYTHON_ENABLE_HARDWARE_BINDINGS 0
#endif

#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_HARDWARE_BINDINGS
#include <esp_log.h>
#include "micropython_lunaredu_module.h"
#include "lunaros_micropython_native_modules.h"
#endif

void setup_micropython_hardware_bindings() {
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_HARDWARE_BINDINGS
  static bool registered = false;
  if (registered) {
    return;
  }

  const MicroPythonNativeModuleDescriptor descriptor = {
      "lunaredu",
      setup_micropython_lunaredu_module,
      retire_micropython_lunaredu_module,
  };
  registered = register_micropython_native_module(descriptor);
  if (!registered) {
    ESP_LOGE("micropython_hw", "failed to register lunaredu python module");
  }
#endif
}
