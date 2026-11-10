#pragma once

#include <stddef.h>

using MicroPythonNativeModuleSetup = void (*)();

struct MicroPythonNativeModuleDescriptor {
  const char *name;
  MicroPythonNativeModuleSetup setup;
  // Called on the VM owner before reset or shutdown. No VM state may escape.
  void (*retire)()=nullptr;
};

using MicroPythonNativeModuleVisitor =
    void (*)(const MicroPythonNativeModuleDescriptor &descriptor,
             void *context);

bool register_micropython_native_module(
    const MicroPythonNativeModuleDescriptor &descriptor);
bool micropython_native_module_registered(const char *name);
void visit_micropython_native_modules(MicroPythonNativeModuleVisitor visitor,
                                      void *context);
void setup_micropython_native_modules();

void retire_micropython_native_modules();
