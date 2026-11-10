#include "espnow_diagnostics.h"
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW && CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT
#include "lunaros_network_espnow_management.h"
#include "diagnostics/session.h"
#include "official_tools.h"
#include "trace_retention.h"
#include "kernel/static_mutex.h"
#include <esp_timer.h>
#include <array>
#include <algorithm>
#include <cstring>
namespace lunaredu::lunaros::internal {
namespace {
StaticMutex mutex;
bool ready=false;
struct Command {
  uint8_t kind=0;
  uint32_t duration=0,generation=0;
  std::array<char,33> token{};
};
Command pending;
uint8_t last_result=0; // 0=no failure/accepted, 1=revoked, 2=SDK failure.
void wipe(Command &command){volatile char *bytes=command.token.data();for(size_t i=0;i<command.token.size();++i)bytes[i]=0;command={};}
uint32_t get32(const uint8_t *in){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(in[i])<<(8*i);return value;}
void put32(uint8_t *out,uint32_t value){for(unsigned i=0;i<4;++i)out[i]=uint8_t(value>>(8*i));}
bool handle(const EspNowManagementRequest &request,uint8_t *output,size_t &size,std::string &error){
  if(!request.data||request.size<33||size<28){error="espnow_diagnostics_invalid_request";return false;}
  Command command;command.kind=request.data[0];std::memcpy(command.token.data(),request.data+1,32);
  if(std::memchr(command.token.data(),0,32)||!diagnostic_session_token_valid(command.token.data())){wipe(command);error="debug_session_required";return false;}
  if((command.kind==1||command.kind==3)?request.size!=33:command.kind!=2||request.size!=37){wipe(command);return false;}
  if(command.kind==2){command.duration=get32(request.data+33);if(command.duration<100||command.duration>60000){wipe(command);return false;}}
  const auto management=espnow_management_status();command.generation=management.generation;
  {
    StaticLockGuard lock(mutex);
    if(!ready||(command.kind!=1&&pending.kind)){wipe(command);error="espnow_diagnostics_busy";return false;}
    if(command.kind!=1){pending=command;last_result=0;}
    output[2]=pending.kind;output[3]=last_result;
  }
  wipe(command);
  const auto facts=official_tools::status();const auto retained=trace_retention::status();
  output[0]=0;output[1]=1;output[4]=facts.active;output[5]=facts.retiring;output[6]=retained.pending;output[7]=retained.truncated;
  put32(output+8,facts.generation);put32(output+12,facts.remaining_ms);put32(output+16,facts.emitted);
  put32(output+20,facts.dropped);put32(output+24,uint32_t(retained.bytes));size=28;return true;
}
}
bool start_espnow_diagnostics(){
  if(!espnow_bind_management_provider(EspNowManagementFamily::Diagnostics,handle))return false;
  StaticLockGuard lock(mutex);ready=true;return true;
}
void stop_espnow_diagnostics(){StaticLockGuard lock(mutex);ready=false;wipe(pending);}
void poll_espnow_diagnostics(){
  Command command;
  {StaticLockGuard lock(mutex);if(!ready||!pending.kind)return;command=pending;wipe(pending);}
  const auto management=espnow_management_status();bool ok=false;uint8_t result=1;
  if(management.active&&management.authenticated&&management.generation==command.generation&&diagnostic_session_token_valid(command.token.data())){
    std::string error;
    if(command.kind==3)ok=official_tools::stop(error);
    else{
      const auto authorization=diagnostic_session_status();
      const auto duration=std::min({command.duration,authorization.session_remaining_ms,management.remaining_ms});
      if(duration>=100)ok=official_tools::start(duration,error,uint64_t(esp_timer_get_time())/1000+duration);
    }
    result=ok?0:2;
  }
  wipe(command);StaticLockGuard lock(mutex);last_result=result;
}
}
#endif
