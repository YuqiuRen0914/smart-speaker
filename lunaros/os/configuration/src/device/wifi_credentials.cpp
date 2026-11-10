#include "configuration/wifi_credentials.h"

namespace lunaredu::lunaros {
bool valid_device_wifi_credentials(std::string_view ssid, std::string_view password) {
  return !ssid.empty() && ssid.size()<=kDeviceWifiSsidMax &&
      ssid.find('\0')==std::string_view::npos && password.find('\0')==std::string_view::npos &&
      (password.empty() || (password.size()>=8 && password.size()<=63));
}

DeviceWifiListValidity validate_device_wifi_list(const DeviceWifiCredential *items,size_t count) {
  if(!items || !count || count>kDeviceWifiCredentialMax) return DeviceWifiListValidity::Invalid;
  for(size_t i=0;i<count;++i) {
    if(!valid_device_wifi_credentials(items[i].ssid,items[i].password)) return DeviceWifiListValidity::Invalid;
    for(size_t j=0;j<i;++j) if(items[i].ssid==items[j].ssid) return DeviceWifiListValidity::Duplicate;
  }
  return DeviceWifiListValidity::Valid;
}

bool valid_device_wifi_list(const DeviceWifiCredential *items,size_t count) {
  return validate_device_wifi_list(items,count)==DeviceWifiListValidity::Valid;
}
}
