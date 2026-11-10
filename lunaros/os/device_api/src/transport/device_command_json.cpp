#include "device_api/device_command_json.h"

namespace lunaredu::lunaros {
bool invoke_device_command_json(const DeviceCommandExtension &extension,
    const std::string &name, JsonVariantConst input, JsonObject output, std::string &error) {
  const DeviceCommandInput values{&input,
    [](const void *p,const char *key,std::string &value) {
      const auto field=(*static_cast<const JsonVariantConst *>(p))[key];
      if(!field.is<const char *>())return false;
      value=field.as<const char *>(); return true;
    },
    [](const void *p,const char *key,bool &value) {
      const auto field=(*static_cast<const JsonVariantConst *>(p))[key];
      if(!field.is<bool>())return false;
      value=field.as<bool>(); return true;
    }};
  const DeviceCommandOutput result{&output,
    [](void *p,const char *key,const std::string &value){(*static_cast<JsonObject *>(p))[key]=value;},
    [](void *p,const char *key,bool value){(*static_cast<JsonObject *>(p))[key]=value;},
    [](void *p,const char *key,uint32_t value){(*static_cast<JsonObject *>(p))[key]=value;}};
  return extension.invoke && extension.invoke(name,values,result,error);
}
}
