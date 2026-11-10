#include "lunaros_factory.h"
#include "device_api/device_commands.h"
namespace lunaredu::lunaros {
namespace {
bool handle_usb_factory_command(const std::string &name,
                                const DeviceCommandInput &input, const DeviceCommandOutput &result,
                                std::string &error) {
  if (name != "factory_mode.enter" && name != "factory_mode.exit" &&
      name != "factory_mode.start" && name != "factory_mode.confirm" &&
      name != "factory_mode.reset") return false;

  auto &mode = local_factory_mode();
  const auto status = mode.status();
  if (status.active && status.entry != FactoryEntry::Usb) {
    error = "factory_mode_owned_by_web";
    return true;
  }
  if (name == "factory_mode.enter") {
    if (!status.active && !mode.enter(FactoryEntry::Usb, error)) return true;
  } else {
    if (!status.active) {
      error = "factory_mode_inactive";
      return true;
    }
    if (name == "factory_mode.exit") {
      mode.leave();
    } else {
      std::string id;
      if (!input.read_string || !input.read_string(input.context,"id",id) || id.empty()) {
        error = "factory_check_id_required";
        return true;
      }
      if (name == "factory_mode.start") {
        std::string action;
        if(input.read_string)(void)input.read_string(input.context,"action",action);
        if (!mode.start(id.c_str(), action.c_str(), error)) return true;
      } else if (name == "factory_mode.confirm") {
        bool passed=false;
        if (!input.read_boolean || !input.read_boolean(input.context,"passed",passed)) {
          error = "factory_check_result_required";
          return true;
        }
        if (!mode.confirm(id.c_str(), passed, error)) return true;
      } else if (!mode.reset(id.c_str(), error)) {
        return true;
      }
      FactoryCheckSnapshot check;
      if (mode.check(id.c_str(), check)) {
        if(result.write_string){
          result.write_string(result.context,"id",check.id);
          result.write_string(result.context,"state",factory_check_state_name(check.state));
        }
        if(result.write_number)result.write_number(result.context,"run_count",check.run_count);
      }
    }
  }
  if(result.write_boolean)result.write_boolean(result.context,"active",mode.status().active);
  if(result.write_string)result.write_string(result.context,"entry","usb");
  return true;
}

}
const DeviceCommandExtension &factory_device_commands(){
 static const DeviceCommandExtension extension{"factory.mode", "factory_mode.enter, factory_mode.start, factory_mode.confirm, factory_mode.reset, factory_mode.exit",
 [](const std::string &name){return name=="factory_mode.enter"||name=="factory_mode.start"||name=="factory_mode.confirm"||name=="factory_mode.reset"||name=="factory_mode.exit";},handle_usb_factory_command};
 return extension;
}
}
