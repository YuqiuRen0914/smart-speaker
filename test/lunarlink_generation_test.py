"""Run the production asynchronous bootstrap job across identity changes."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'lunaros_apps/lunaredu_pet/src/interaction/conversation/assistant_lifecycle.cpp').read_text()
a=s.index('lunaredu::lunaros::NetworkJobRunResult run_activation_job(')
b=s.index('\nvoid maybe_submit_activation_locked',a)
job=s[a:b]
fixture=r'''
#include "interaction/transport/assistant_provider.h"
#include "interaction/transport/assistant_activation_client.h"
#include <cassert>
#include <cinttypes>
#include <iostream>
#include <mutex>
#include <string>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
constexpr const char *kDefaultAssistantOtaUrl="https://gateway.example/wire/v1/device/bootstrap";
constexpr int kSystemTimeSyncTimeoutMs=100,kActivationFailureRetryMaxMs=30000;
int64_t now_ms(){return 1000;}
namespace lunaredu::lunaros {
struct NetworkJobHandle {};
struct NetworkJobRunResult {bool ok=false,retry=false;std::string detail;};
}
namespace lunaredu::system {struct SystemInfo {static std::string GetMacAddress(){return "00:11:22:33:44:55";}};}
struct bsp_board_info_t {const char *board_type="speaker";};
const bsp_board_info_t *bsp_board_get_info(){static bsp_board_info_t board;return &board;}
struct Identity {
 std::string serial="LUNAR-TEST",credential="token-test",environment="production",account="account-test";int epoch=5;
 bool ready()const{return epoch>0&&!credential.empty()&&!account.empty();}
 bool operator==(const Identity &)const=default;
} live;
Identity lunarlink_identity(){return live;}
struct Runtime {
 std::mutex mutex;
 bool service_enabled=true,service_suspended=false;
 uint32_t platform_generation=1;
 Identity platform_identity;
 std::string device_id="LUNAR-TEST",client_id="test_uuid";
 struct Activation {
  bool ready=false,user_blocked=false,request_inflight=true,pending=true,configured=false,result_ok=false,result_available=false;
  std::string ota_url,code;int64_t next_attempt_at_ms=0;uint32_t timeout_ms=30000;XiaozhiActivationResult result;
 }activation;
 struct {std::string last_error;}diagnostics;
}g_runtime;
bool assistant_synchronize_network_clock(int,std::string &){return true;}
int change=0;
bool xiaozhi_activate_device(const XiaozhiActivationRequest &request,XiaozhiActivationResult &result){
 assert(request.binding_epoch==5&&request.device_token=="token-test"&&request.device_id=="LUNAR-TEST");
 result.ok=result.has_mqtt=true;result.binding_epoch=5;result.mqtt_endpoint="test-broker";
 if(change==1)++g_runtime.platform_generation;
 if(change==2)live.epoch=6;
 if(change==3)live.credential="replacement-token";
 if(change==4)live.account="new-account";
 if(change==5)g_runtime.activation.user_blocked=true;
 return true;
}
'''+job+r'''
int main(){
 for(change=0;change<=5;++change){
  live=Identity{};g_runtime.platform_identity=live;g_runtime.activation={};g_runtime.platform_generation=1;
  const auto result=run_activation_job({},nullptr);
  if(change==0)assert(result.ok&&g_runtime.activation.result_available&&g_runtime.activation.result_ok);
  else assert(!result.ok&&!g_runtime.activation.result_available);
 }
 live=Identity{};live.account.clear();g_runtime.platform_identity=live;g_runtime.activation={};
 const auto unbound=run_activation_job({},nullptr);assert(!unbound.ok&&!g_runtime.activation.request_inflight);
 live=Identity{};live.environment="development";g_runtime.platform_identity=live;g_runtime.activation={};
 const auto development=run_activation_job({},nullptr);assert(!development.ok&&development.detail=="lunarlink_development_endpoint_required");
 std::cout<<"LunarLink bootstrap generation: normal result accepted; late reset, rebind, credential/account change and local suspension rejected\n";
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-bootstrap-generation-') as tmp:
    p=Path(tmp)/'test.cpp';p.write_text(fixture)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-O2',
                    '-I'+str(root/'lunaros_apps/lunaredu_pet/src'),str(p),'-o',str(p.with_suffix(''))],check=True)
    subprocess.run([str(p.with_suffix(''))],check=True)
