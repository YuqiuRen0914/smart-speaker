#include "device_api/access/device_api_maintenance_access.h"
#include "device_api/access/maintenance_authority.h"
#include "configuration/device_config.h"
#include "system/identity.h"
#include "platform/system.h"
#include "lunaros_device_api_service_id.h"
#include "lunaros_network_http_client.h"
#include "lunaros_network_jobs.h"
#include <esp_random.h>
#include <ctime>
#include <mutex>

namespace {
using namespace lunaredu::lunaros;
using namespace lunaredu::lunaros::local_web;
MaintenanceAuthority authority;
enum class TrustState {Unknown,Loading,Ready,Unsupported,Failed};
std::mutex trust_mutex;
TrustState trust_state=TrustState::Unknown;
uint32_t last_attempt=0;
NetworkJobHandle trust_job;

std::array<uint8_t,32> entropy() {
  std::array<uint8_t,32> bytes{};esp_fill_random(bytes.data(),bytes.size());return bytes;
}
bool fetch_fixed(const char *url,JsonDocument &document,int *http_status=nullptr) {
  NetworkFetchRequest request;
  request.url=url;request.timeout_ms=8000;request.maximum_response_bytes=16384;
  request.system_trust_only=true;
  NetworkResponse response;
  const bool fetched=network_fetch_resource(std::move(request),&response);
  if(http_status)*http_status=response.status_code;
  return fetched&&response.status_code==200&&
      !deserializeJson(document,response.body)&&document.is<JsonObjectConst>();
}
NetworkJobRunResult load_trust(NetworkJobHandle,void *) {
  JsonDocument capabilities,jwks;
  TrustState next=TrustState::Failed;
  std::string error="maintenance_authority_unavailable";
  int capability_status=-1;
  if(fetch_fixed("https://passport.lunaredu.cn/api/device/authorize",capabilities,&capability_status)) {
    const auto contract=capabilities["data"]["maintenance"];
    bool protocol2=false;
    for(auto value:capabilities["data"]["protocols"].as<JsonArrayConst>())if(value.is<int>()&&value.as<int>()==2)protocol2=true;
    if(!protocol2||!(contract["available"]|false)||contract["protocol"].as<int>()!=2||
        std::string(contract["issuer"]|"")!=kMaintenanceIssuer||std::string(contract["jwks_uri"]|"")!=kMaintenanceJwks||
        std::string(contract["algorithm"]|"")!="RS256"||std::string(contract["typ"]|"")!=kMaintenanceType||
        std::string(contract["audience"]|"")!=kMaintenanceAudience||std::string(contract["purpose"]|"")!="device.maintenance") {
      next=TrustState::Unsupported;error="maintenance_protocol_unavailable";
    } else if(fetch_fixed(kMaintenanceJwks,jwks)) {
      std::string text;serializeJson(jwks,text);
      if(authority.install_trusted_jwks(text,system_monotonic_ms(),error))next=TrustState::Ready;
    }
  } else if(capability_status==404||capability_status==405||capability_status==501) {
    next=TrustState::Unsupported;error="maintenance_protocol_unavailable";
  }
  {std::lock_guard<std::mutex> lock(trust_mutex);trust_state=next;last_attempt=system_monotonic_ms();}
  return {next==TrustState::Ready,false,next==TrustState::Ready?"maintenance_trust_ready":error};
}
bool ensure_trust(std::string &error,bool force=false) {
  const auto now=system_monotonic_ms();
  if(!force&&authority.keys_ready(now)) {
    std::lock_guard<std::mutex> lock(trust_mutex);
    if(trust_state==TrustState::Ready){error.clear();return true;}
  }
  {
    std::lock_guard<std::mutex> lock(trust_mutex);
    if(trust_state==TrustState::Loading){
      // Admission can expire/cancel a queued job without invoking its runner.
      // Do not leave the maintenance authority stuck in Loading forever.
      if(static_cast<uint32_t>(now-last_attempt)>=25000&&!network_job_active(trust_job)) {
        trust_state=TrustState::Failed;last_attempt=now-5000;
      } else {error="maintenance_trust_loading";return false;}
    }
    if((trust_state==TrustState::Failed||trust_state==TrustState::Unsupported)&&static_cast<uint32_t>(now-last_attempt)<5000){
      error=trust_state==TrustState::Unsupported?"maintenance_protocol_unavailable":"maintenance_authority_unavailable";return false;}
    trust_state=TrustState::Loading;last_attempt=now;
  }
  NetworkJobDefinition job;
  job.owner_service_id=kDeviceApiServiceId;job.kind="maintenance_trust";
  job.priority=kNetworkPriorityInteractive;job.maximum_attempts=1;
  job.deadline_ms=now+20000;job.runner=load_trust;
  NetworkJobHandle handle;std::string submission;
  if(!submit_network_job(job,handle,submission)) {
    std::lock_guard<std::mutex> lock(trust_mutex);trust_state=TrustState::Failed;
    error="maintenance_trust_admission_unavailable";return false;
  }
  {std::lock_guard<std::mutex> lock(trust_mutex);trust_job=handle;}
  error="maintenance_trust_loading";return false;
}
}

const char *device_api_maintenance_readiness_error() {
  if(std::time(nullptr)<1700000000)return "device_time_unavailable";
  if(lunaredu::lunaros::device_identity_config().local_device_uuid.empty())return "maintenance_identity_unavailable";
  std::lock_guard<std::mutex> lock(trust_mutex);
  // A previously old/offline deployment must not permanently hide recovery
  // after Passport is upgraded. This status never gates ordinary local OTA.
  if(static_cast<uint32_t>(lunaredu::lunaros::system_monotonic_ms()-last_attempt)<30000) {
    if(trust_state==TrustState::Unsupported)return "maintenance_protocol_unavailable";
    if(trust_state==TrustState::Failed)return "maintenance_authority_unavailable";
  }
  return "";
}
bool device_api_maintenance_request(const std::string &state,const std::string &callback,const std::string &opener,JsonObject response,std::string &error) {
  const auto *readiness=device_api_maintenance_readiness_error();
  if(readiness[0]){error=readiness;return false;}
  if(!ensure_trust(error))return false;
  return authority.issue(lunaredu::lunaros::hardware_serial_number(),
      lunaredu::lunaros::device_identity_config().local_device_uuid,state,callback,opener,
      entropy(),lunaredu::lunaros::system_monotonic_ms(),std::time(nullptr),response,error);
}
bool device_api_maintenance_exchange(const std::string &token,const std::string &state,JsonObject response,std::string &error) {
  if(!ensure_trust(error))return false;
  const bool accepted=authority.exchange(token,state,entropy(),lunaredu::lunaros::system_monotonic_ms(),std::time(nullptr),response,error);
  if(!accepted&&error=="maintenance_key_unavailable") {
    // A newly rotated signing key may arrive between launch and return.
    // Refresh the fixed authority; the still-unused challenge may be retried.
    (void)ensure_trust(error,true);
  }
  return accepted;
}
bool device_api_maintenance_session_authorized(const char *token) {
  return authority.authorized(token,lunaredu::lunaros::system_monotonic_ms(),std::time(nullptr));
}
void device_api_maintenance_session_revoke(const char *token){authority.revoke(token);}
