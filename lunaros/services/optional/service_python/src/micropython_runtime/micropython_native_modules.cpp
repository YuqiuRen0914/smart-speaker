#include "lunaros_micropython_native_modules.h"

#include <string.h>

#include <esp_log.h>

namespace {

constexpr size_t kMaxNativeModules = 8;
MicroPythonNativeModuleDescriptor g_modules[kMaxNativeModules];
size_t g_module_count = 0;

bool same_module_name(const char *left, const char *right) {
  return left != nullptr && right != nullptr && strcmp(left, right) == 0;
}

} // namespace

bool register_micropython_native_module(
    const MicroPythonNativeModuleDescriptor &descriptor) {
  if (descriptor.name == nullptr || descriptor.name[0] == '\0' ||
      descriptor.setup == nullptr) {
    return false;
  }

  for (size_t index = 0; index < g_module_count; ++index) {
    if (same_module_name(g_modules[index].name, descriptor.name)) {
      g_modules[index] = descriptor;
      return true;
    }
  }

  if (g_module_count >= kMaxNativeModules) {
    ESP_LOGE("micropython", "native module registry full name=%s",
         descriptor.name);
    return false;
  }

  g_modules[g_module_count++] = descriptor;
  return true;
}

bool micropython_native_module_registered(const char *name) {
  for (size_t index = 0; index < g_module_count; ++index) {
    if (same_module_name(g_modules[index].name, name)) {
      return true;
    }
  }
  return false;
}

void visit_micropython_native_modules(MicroPythonNativeModuleVisitor visitor,
                                      void *context) {
  if (visitor == nullptr) {
    return;
  }
  for (size_t index = 0; index < g_module_count; ++index) {
    visitor(g_modules[index], context);
  }
}

void setup_micropython_native_modules() {
  for (size_t index = 0; index < g_module_count; ++index) {
    if (g_modules[index].setup != nullptr) {
      g_modules[index].setup();
    }
  }
}

void retire_micropython_native_modules(){
  for(size_t i=g_module_count;i>0;--i)if(g_modules[i-1].retire)g_modules[i-1].retire();
}
