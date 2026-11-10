#include "lunaros_settings_device_api.h"
#include "device_api/device_projection.h"
#include "device_api/device_operation_builders.h"
#include <cstring>
namespace lunaredu::settings {
namespace {
using namespace lunaros;
DesktopSettingsPort port;
void read(DeviceApiContext &,JsonObject out){
  const auto p=port.read();out["startup_application"]=p.startup_id;
  out["animations"]=p.animations;out["show_labels"]=p.show_labels;
  out["text_size"]=p.text_size;out["auto_hide_status"]=p.auto_hide_status;
  out["honeycomb_lines"]=p.honeycomb_lines;
}
DeviceOperationResult write(DeviceApiContext &device,JsonVariantConst request,JsonObject out){
  if(!request.is<JsonObjectConst>())return {false,400,"invalid_desktop_preferences"};
  auto p=port.read();
  for(auto field:request.as<JsonObjectConst>()){
    const std::string key=field.key().c_str();
    if(key=="startup_application"){
      if(!field.value().is<const char *>())return {false,400,"invalid_startup_application"};
      const char *id=field.value().as<const char *>();
      if(std::strlen(id)>=sizeof(p.startup_id)||!port.startup_available(id))return {false,400,"startup_application_unavailable"};
      std::strcpy(p.startup_id,id);
    }else if(key=="text_size"){
      if(!field.value().is<int>()||field.value().as<int>()<0||field.value().as<int>()>2)return {false,400,"invalid_text_size"};
      p.text_size=field.value().as<int>();
    }else if(key=="animations"||key=="show_labels"||key=="auto_hide_status"||key=="honeycomb_lines"){
      if(!field.value().is<bool>())return {false,400,"invalid_desktop_preference"};
      const bool value=field.value().as<bool>();
      if(key=="animations")p.animations=value;
      else if(key=="show_labels")p.show_labels=value;
      else if(key=="honeycomb_lines")p.honeycomb_lines=value;
      else p.auto_hide_status=value;
    }else return {false,400,"unknown_desktop_preference"};
  }
  std::string error;if(!port.apply(p,error))return {false,409,error};
  read(device,out);return {};
}
constexpr DeviceOperationDefinition operations[]={
  make_device_json_provider("preferences.get",read),
  make_device_json_command("preferences.set",write)};
const DeviceCapabilityDescriptor capability=[](){DeviceCapabilityDescriptor c;c.id="desktop_settings";return c;}();
const DeviceModuleProjectionMetadata module={"desktop_settings","desktop_settings",nullptr};
const DeviceModuleProjection projection={"desktop_settings",&module,operations,std::size(operations)};
}
bool register_desktop_settings_projection(lunaros::DeviceProjectionRegistry &registry,
    DesktopSettingsPort selected,std::string &error){
  if(!selected.read||!selected.apply||!selected.startup_available){error="desktop_settings_port_missing";return false;}
  port=selected;return registry.register_projection({&capability,&projection,nullptr,true},&error);
}
}
