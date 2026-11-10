#include "device_api/device_commands.h"
namespace lunaredu::lunaros {
namespace { const DeviceCommandExtension *extension=nullptr; }
bool install_device_command_extension(const DeviceCommandExtension &value) {
  if(!value.capability||!value.commands||!value.accepts||!value.invoke||(extension&&extension!=&value))return false;
  extension=&value;return true;
}
bool remove_device_command_extension(const DeviceCommandExtension &value) {
  if(extension!=&value)return false;
  extension=nullptr;return true;
}
const DeviceCommandExtension *device_command_extension(){return extension;}
}
