#include "desktop/ui/provisioning_view.h"
#include <mbedtls/platform_util.h>

namespace lunaredu::desktop {
std::string provisioning_payload(ProvisioningStep step,std::string_view ssid,
    std::string_view ap_password,std::string_view ip) {
  if(step==ProvisioningStep::Hotspot){
    return lunaros::qr::wifi_payload(ssid, ap_password);
  }
  unsigned parts=0,value=0,digits=0;
  for(char c:ip){
    if(c=='.'){if(!digits||value>255||++parts>3)return {};value=digits=0;}
    else if(c>='0'&&c<='9'){value=value*10+unsigned(c-'0');if(++digits>3)return {};}
    else return {};
  }
  if(parts!=3||!digits||value>255)return {};
  return "http://"+std::string(ip)+"/setup";
}

void ProvisioningQr::update_url(const std::string &url) {
    if (cache_key_ != url) { clear(); cache_key_ = url; qr.generate(url, 4); }
  }

void ProvisioningQr::update(bool active,ProvisioningStep step,std::string_view ssid,
      std::string_view password,std::string_view ip) {
    if(!active){clear();return;}
    auto hotspot=provisioning_payload(ProvisioningStep::Hotspot,ssid,password,ip);
    auto key=hotspot+"|"+std::string(ip)+(step==ProvisioningStep::Hotspot?"|1":"|2");
    if(key!=cache_key_){
      // Keep the temporary's length so even a small-string move cannot hide
      // bytes that must be scrubbed below.
      clear();cache_key_=key;
      qr.generate(hotspot,4);
      const int version=qr.ready()?(qr.size()-17)/4:4;
      if(step==ProvisioningStep::Setup){
        auto address=provisioning_payload(step,ssid,password,ip);
        qr.generate(address,version);
      }
    }
    // Erase temporary copies of the device hotspot secret.
    if(!hotspot.empty())mbedtls_platform_zeroize(hotspot.data(),hotspot.size());
    if(!key.empty())mbedtls_platform_zeroize(key.data(),key.size());
  }

void ProvisioningQr::clear() {qr.clear();if(!cache_key_.empty())mbedtls_platform_zeroize(cache_key_.data(),cache_key_.size());cache_key_.clear();}

ProvisioningQr::~ProvisioningQr(){clear();}
}
