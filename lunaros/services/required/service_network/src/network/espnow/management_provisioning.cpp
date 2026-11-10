#include "sdkconfig.h"
#include "network/espnow/management_internal.h"
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
#include "lunaros_network_wifi_configuration.h"
#include "network/network_service_internal.h"
#include <array>
#include <cstring>
#include <mbedtls/platform_util.h>
namespace lunaredu::lunaros::internal {
namespace {
void put32(uint8_t *out,uint32_t value){for(unsigned i=0;i<4;++i)out[i]=uint8_t(value>>(8*i));}
struct Credentials {
  std::array<char,33> ssid{};
  std::array<char,65> password{};
  ~Credentials(){mbedtls_platform_zeroize(password.data(),password.size());}
};
bool handle(const EspNowManagementRequest &request,uint8_t *output,size_t &size,std::string &error){
  if(!request.data||!request.size||size<11){error="espnow_provisioning_invalid_request";return false;}
  if(request.data[0]==1){
    // [1, SSID byte length, password byte length, SSID, password]. Embedded
    // NUL and trailing bytes are rejected before the canonical validator.
    if(request.size<3)return false;
    const size_t ssid_size=request.data[1],password_size=request.data[2];
    if(!ssid_size||ssid_size>32||password_size>64||request.size!=3+ssid_size+password_size||
        std::memchr(request.data+3,0,ssid_size+password_size))return false;
    auto *runtime=network_service_internal().owner_runtime();if(!runtime)return false;
    Credentials credentials;
    std::memcpy(credentials.ssid.data(),request.data+3,ssid_size);
    std::memcpy(credentials.password.data(),request.data+3+ssid_size,password_size);
    uint32_t attempt=0;
    const auto result=request_network_connect_wifi(*runtime,credentials.ssid.data(),credentials.password.data(),attempt);
    if(result!=NetworkWifiCredentialResult::Ok){error="espnow_provisioning_not_admitted";return false;}
    output[0]=0;put32(output+1,attempt);size=5;return true;
  }
  if(request.data[0]==2&&request.size==1){
    const auto facts=network_wifi_connection_snapshot();output[0]=0;output[1]=uint8_t(facts.phase);
    put32(output+2,facts.attempt);put32(output+6,facts.sequence);size=10;return true;
  }
  error="espnow_provisioning_unknown_command";return false;
}
}
bool bind_espnow_provisioning(){return espnow_bind_management_provider(EspNowManagementFamily::Provisioning,handle);}
}
#else
namespace lunaredu::lunaros::internal {bool bind_espnow_provisioning(){return false;}}
#endif
