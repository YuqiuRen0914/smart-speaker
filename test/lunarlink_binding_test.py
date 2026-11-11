"""Exercise actual Board pairing/epoch revocation without network or credentials."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'lunaros/src/device_link/device_link_pairing.cpp').read_text()
source='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
fixture=r'''
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#define LOGI(...) ((void)0)
namespace lunaredu::lunaros::device_link {
enum class DeviceLinkBindingState {Bound,Unbound,Error,Pairing};
struct DeviceLinkPairingSnapshot {DeviceLinkBindingState state=DeviceLinkBindingState::Unbound;int binding_epoch=0;std::string account_id,error,bind_url;};
struct DeviceLinkConfig {std::string fixed_pairing_code,board_type="speaker",model="speaker";};
struct DeviceConfig {struct {std::string credential="test_credential";}cloud;struct {std::string name="test";}identity;};
DeviceLinkPairingSnapshot binding;
DeviceLinkConfig config;
DeviceConfig device;
std::string response,path,method,body;
bool network_ok=true;
int status=200;
namespace internal {uint32_t system_random_u32(){return 12345;}}
bool ensure_network_clock(std::string &){return true;}
bool account_identity_ready(){return true;}
const DeviceConfig &device_config(){return device;}
const DeviceLinkConfig &runtime_config(){return config;}
const char *hardware_serial_number(){return "LUNAR-TEST";}
const char *firmware_version(){return "1.0.35";}
const char *platform_base_url(){return "https://board.example";}
void add_capabilities(JsonArray a){a.add("smart_speaker");}
DeviceLinkPairingSnapshot pairing_snapshot(){return binding;}
void set_private_resource_scope(uint32_t,const char*){}
void update_pairing_snapshot(DeviceLinkBindingState state,const std::string & ={},const std::string &url={},int64_t=0,const std::string &error={},int epoch=-1,const std::string &account={},const std::string & ={},const std::string & ={},bool=false){
 binding.state=state;binding.error=error;binding.bind_url=url;
 if(epoch>=0)binding.binding_epoch=epoch;
 if(!account.empty())binding.account_id=account;
 if(state==DeviceLinkBindingState::Unbound)binding.account_id.clear();
}
DeviceLinkBindingState state_from_server(const std::string &s){return s=="active"?DeviceLinkBindingState::Bound:s=="unbound"?DeviceLinkBindingState::Unbound:DeviceLinkBindingState::Error;}
bool perform_request(const char *m,const std::string &p,const std::string *b,JsonDocument *out,int *code=nullptr){method=m;path=p;body=b?*b:"";if(code)*code=status;if(out)assert(!deserializeJson(*out,response));return network_ok;}
bool perform_public_request(const char *m,const std::string &p,const std::string *b,JsonDocument *out,int *code){return perform_request(m,p,b,out,code);}
}
'''+source+r'''
int main(){using namespace lunaredu::lunaros::device_link;
 response=R"({"data":{"pairing":{"code":"123456","bindPath":"/bind/123456","expiresAtMs":1234}}})";
 assert(create_pairing_session());assert(path=="/api/link/device-enrollment/pairing-sessions"&&binding.bind_url=="https://board.example/bind/123456");
 response=R"({"data":{"pairing":{"code":"123456","bindPath":"//evil.test","expiresAtMs":1234}}})";
 assert(!create_pairing_session()&&binding.error=="pairing_response_invalid");
 response=R"({"data":{"deviceStatus":"active","bindingEpoch":5,"boundAccount":{"id":"account-test"}}})";
 assert(poll_pairing_session()&&binding.binding_epoch==5&&binding.state==DeviceLinkBindingState::Bound);
 assert(path=="/api/link/device-runtime/pairing-sessions");
 response=R"({"data":{"deviceStatus":"active","bindingEpoch":6,"boundAccount":{"id":""}}})";
 assert(!poll_pairing_session());assert(binding.binding_epoch==5);
 binding.state=DeviceLinkBindingState::Bound;
 response=R"({"data":{"expectedBindingEpoch":4,"bindingEpoch":6,"state":"unbound"}})";
 assert(!unbind_device()&&binding.binding_epoch==5);assert(path=="/api/link/device-runtime/binding/revocation");
 JsonDocument sent;assert(!deserializeJson(sent,body));assert(sent["expectedBindingEpoch"]==5);
 binding.state=DeviceLinkBindingState::Bound;
 response=R"({"data":{"expectedBindingEpoch":5,"bindingEpoch":6,"state":"unbound"}})";
 assert(unbind_device()&&binding.binding_epoch==6&&binding.account_id.empty());
 assert(cancel_pairing_session()&&method=="DELETE"&&path=="/api/link/device-runtime/pairing-sessions");
 std::cout<<"Board binding: new paths, pairing URL admission, account/epoch validation and stale revocation rejection passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-binding-') as tmp:
    cpp=Path(tmp)/'test.cpp';cpp.write_text(fixture)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-const-variable','-O2',
                    '-I'+str(root/'managed_components/bblanchon__arduinojson/src'),str(cpp),'-o',str(cpp.with_suffix(''))],check=True)
    subprocess.run([str(cpp.with_suffix(''))],check=True)
