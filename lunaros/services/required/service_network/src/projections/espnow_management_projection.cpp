#include "projections/espnow_management_projection.h"
#include "device_api/device_api_context.h"
#include "lunaros_network_espnow_management.h"
#include "diagnostics/session.h"
#include "configuration/device_config.h"
#include "kernel/static_mutex.h"
#include <array>
#include <cstring>
#include <mbedtls/platform_util.h>
namespace lunaredu::lunaros::internal {
namespace {
// Process-lifetime native authorization context, never a borrowed request.
// One Main-dispatched projection mutates it; Network only checks this gate.
StaticMutex gate_mutex;
std::array<char,33> token{};
uint64_t credential=0;
uint32_t owned_generation=0;
void clear_gate(bool retired=true){StaticLockGuard lock(gate_mutex);mbedtls_platform_zeroize(token.data(),token.size());credential=0;if(retired)owned_generation=0;}
bool authorized(void*){
  StaticLockGuard lock(gate_mutex);
  return credential==device_cloud_credential_fingerprint()&&diagnostic_session_token_valid(token.data());
}
int nibble(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
}
void fill_espnow_management(DeviceApiContext &,JsonObject output){
  const auto facts=espnow_management_status();output["active"]=facts.active;output["authenticated"]=facts.authenticated;
  output["retiring"]=facts.retiring;output["generation"]=facts.generation;output["remaining_ms"]=facts.remaining_ms;
  output["families"]=facts.families;output["accepted"]=facts.accepted;output["rejected"]=facts.rejected;output["last_result"]=facts.last_result;
}
DeviceOperationResult open_espnow_management(DeviceApiContext &device,JsonVariantConst request,JsonObject output){
  if(!request.is<JsonObjectConst>())return device_operation_error("espnow_management_invalid_request",400);
  for(const auto field:request.as<JsonObjectConst>()){
    if(field.key()!="peer_mac"&&field.key()!="proof_of_possession"&&field.key()!="session_token"&&field.key()!="duration_ms"&&field.key()!="families")
      return device_operation_error("espnow_management_unknown_field",400);
  }
  if(!request["peer_mac"].is<const char*>()||!request["proof_of_possession"].is<const char*>()||
      !request["session_token"].is<const char*>()||!request["families"].is<uint8_t>()||
      (!request["duration_ms"].isNull()&&!request["duration_ms"].is<uint32_t>()))return device_operation_error("espnow_management_invalid_request",400);
  const char *mac=request["peer_mac"],*session_token=request["session_token"],*pop=request["proof_of_possession"];
  const auto families=request["families"].as<uint8_t>();
  const size_t pop_length=request["proof_of_possession"].as<JsonString>().size();
  if(request["peer_mac"].as<JsonString>().size()!=17||request["session_token"].as<JsonString>().size()!=32||
      pop_length<8||pop_length>64||std::strlen(pop)!=pop_length)return device_operation_error("espnow_management_invalid_request",400);
  // A diagnostic token authorizes these debug profiles only. OTA admission
  // requires the product's explicit native management/maintenance policy.
  if(!families||(families&~uint8_t(6))||std::strlen(mac)!=17||std::strlen(session_token)!=32||
      !diagnostic_session_token_valid(session_token))return device_operation_error("espnow_management_authority_required",403);
  EspNowManagementWindow window;window.families=families;window.duration_ms=request["duration_ms"]|60000u;
  for(size_t i=0;i<6;++i){
    const auto high=nibble(mac[i*3]),low=nibble(mac[i*3+1]);
    if(high<0||low<0||(i<5&&mac[i*3+2]!=':'))return device_operation_error("espnow_management_invalid_peer",400);
    window.peer[i]=uint8_t((high<<4)|low);
  }
  const auto facts=espnow_management_status();
  if(facts.active||facts.retiring)return device_operation_error("espnow_management_busy",409);
  {StaticLockGuard lock(gate_mutex);std::memcpy(token.data(),session_token,32);token[32]=0;credential=device_cloud_credential_fingerprint();}
  window.proof_of_possession=pop;window.authorized=authorized;
  window.retired=[](void*){clear_gate();};
  std::string error;
  if(!espnow_open_management(device.runtime,window,error)){clear_gate();return device_operation_error(error.c_str(),503);}
  {StaticLockGuard lock(gate_mutex);owned_generation=espnow_management_status().generation;}
  fill_espnow_management(device,output);return device_operation_ack();
}
DeviceOperationResult close_espnow_management(DeviceApiContext &device,JsonVariantConst,JsonObject output){
  const auto facts=espnow_management_status();
  {StaticLockGuard lock(gate_mutex);if((facts.active||facts.retiring)&&facts.generation!=owned_generation)return device_operation_error("espnow_management_native_owner_required",403);}
  // Revocation is immediate, even if SDK key retirement must retry.
  clear_gate(false);std::string error;
  if(!espnow_close_management(device.runtime,error))return device_operation_error(error.c_str(),503);
  fill_espnow_management(device,output);return device_operation_ack();
}
}
