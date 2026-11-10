#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
// Synchronous borrowed scalar ports. Implementations own decoding and storage;
// command providers do not depend on a transport or retain these contexts.
struct DeviceCommandInput {
  const void *context = nullptr;
  bool (*read_string)(const void *, const char *, std::string &) = nullptr;
  bool (*read_boolean)(const void *, const char *, bool &) = nullptr;
};
struct DeviceCommandOutput {
  void *context = nullptr;
  void (*write_string)(void *, const char *, const std::string &) = nullptr;
  void (*write_boolean)(void *, const char *, bool) = nullptr;
  void (*write_number)(void *, const char *, uint32_t) = nullptr;
};
// Borrowed static declaration; install and remove on the function owner executor.
// The transport enforces the declared capability before invoking mutations.
struct DeviceCommandExtension {
  const char *capability;
  const char *commands;
  bool (*accepts)(const std::string &);
  bool (*invoke)(const std::string &, const DeviceCommandInput &, const DeviceCommandOutput &, std::string &);
};
bool install_device_command_extension(const DeviceCommandExtension &);
bool remove_device_command_extension(const DeviceCommandExtension &);
const DeviceCommandExtension *device_command_extension();
}
