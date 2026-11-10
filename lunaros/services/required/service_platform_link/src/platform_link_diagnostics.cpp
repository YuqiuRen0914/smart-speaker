#include "lunaros_platform_link_diagnostics.h"
#include "lunaros_platform_link.h"
#include "platform_link_http.h"
#include "configuration/device_config.h"
#include "system/identity.h"
#include <ArduinoJson.h>
#include <cstring>
namespace lunaredu::lunaros {
bool platform_link_diagnostic_policy(bool &enabled,int &epoch){
 enabled=false;epoch=0;const auto binding=platform_link_binding_status();
 if(binding.state!=PlatformLinkBindingState::Bound || binding.revocation_requested || binding.persistence_pending)return false;
 JsonDocument response;
 if(!platform_link::perform_request("GET","/api/link/device-runtime/diagnostics",nullptr,&response))return false;
 const auto data=response["data"];
 if(!data["enabled"].is<bool>() || !data["bindingEpoch"].is<int>() || data["bindingEpoch"].as<int>()!=binding.binding_epoch)return false;
 const auto current=platform_link_binding_status();
 if(current.revision!=binding.revision || current.revocation_requested)return false;
 enabled=data["enabled"].as<bool>();epoch=binding.binding_epoch;return true;
}
bool platform_link_upload_diagnostic(const char *kind,const char *elf,const char *hash,NetworkBodyStream body,NetworkResponse &response){
 const auto binding=platform_link_binding_status();
 if(binding.state!=PlatformLinkBindingState::Bound || binding.revocation_requested || binding.persistence_pending || !body.read || body.length>2*1024*1024)return false;
 const auto digest=[](const char *value){return value && std::strlen(value)==64 && std::strspn(value,"0123456789abcdef")==64;};
 if(!digest(elf)||!digest(hash)||!kind || (std::strcmp(kind,"heaptrace")&&std::strcmp(kind,"apptrace")&&std::strcmp(kind,"systemview")&&std::strcmp(kind,"coredump")&&std::strcmp(kind,"insights")&&std::strcmp(kind,"gprof")))return false;
 NetworkFetchRequest request;
 request.method="POST";request.url=platform_link::platform_base_url()+"/api/link/device-runtime/diagnostics";
 request.timeout_ms=8000;request.maximum_response_bytes=2048;request.body_stream=body;
 request.headers={{"Authorization","Bearer "+device_cloud_credential()},{"X-Hardware-Serial",hardware_serial_number()},
  {"Content-Type","application/octet-stream"},{"X-Diagnostic-Kind",kind},{"X-Diagnostic-Elf-Sha256",elf},{"X-Diagnostic-Sha256",hash}};
 if(!network_fetch_resource(std::move(request),&response) || response.status_code<200 || response.status_code>=300)return false;
 const auto current=platform_link_binding_status();
 if(current.revision!=binding.revision || current.revocation_requested)return false;
 JsonDocument result;
 return !deserializeJson(result,response.body) && result["data"]["sha256"].is<const char*>() && std::strcmp(result["data"]["sha256"].as<const char*>(),hash)==0;
}
}
