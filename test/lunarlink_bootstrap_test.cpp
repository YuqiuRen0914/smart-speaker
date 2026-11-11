#include "interaction/transport/assistant_activation_client.h"
#include "interaction/transport/assistant_transport.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
using namespace lunaredu::lunaros;
NetworkFetchRequest sent;
std::string response;
int status=200,requests=0;
bool lunaredu::lunaros::network_fetch_resource(NetworkFetchRequest r,NetworkResponse *out){
 sent=std::move(r);++requests;out->status_code=status;out->body=response;return true;
}
int main(){
 JsonDocument body;body["protocol"]="lunaredu.wire";body["family"]="device";body["version"]=1;body["profile"]="mqtt";
 body["binding"]["epoch"]=5;
 for(auto name:{"endpoint","client_id","username","password","publish_topic","subscribe_topic"})body["mqtt"][name]=std::string("test_")+name;
 serializeJson(body,response);
 XiaozhiActivationRequest r;r.ota_url="https://gateway.example/wire/v1/device/bootstrap";r.device_id="LUNAR-TEST";r.mac_address="00:11:22:33:44:55";
 r.client_id="client_uuid";r.device_token="test-token";r.binding_epoch=5;
 XiaozhiActivationResult result;
 assert(xiaozhi_activate_device(r,result)&&result.has_mqtt&&result.binding_epoch==5&&!result.activation_pending);
 assert(sent.url==r.ota_url&&sent.maximum_response_bytes==16384);
 auto header=[](const char *name){for(auto &h:sent.headers)if(h.first==name)return h.second;return std::string{};};
 assert(header("Device-Id")==r.device_id&&header("Device-Token")==r.device_token&&header("Authorization")=="Bearer test-token");
 assert(header("X-Lunaredu-Protocol")=="lunaredu.wire"&&header("X-Lunaredu-Profile")=="mqtt");
 JsonDocument request;assert(!deserializeJson(request,sent.body));assert(request["hardware_serial"]==r.device_id&&request["mac_address"]==r.mac_address&&request["version"]==1);
 r.binding_epoch=6;assert(!xiaozhi_activate_device(r,result));r.binding_epoch=5;
 body["mqtt"]["subscribe_topic"]="";response.clear();serializeJson(body,response);assert(!xiaozhi_activate_device(r,result));
 body["mqtt"]["subscribe_topic"]="down";body["profile"]="xiaozhi";response.clear();serializeJson(body,response);assert(!xiaozhi_activate_device(r,result));
 status=401;assert(!xiaozhi_activate_device(r,result)&&result.error=="lunarlink_bootstrap_http_401");
 const int count=requests;r.device_token.clear();assert(!xiaozhi_activate_device(r,result)&&requests==count);
 std::cout<<"LunarLink bootstrap: bound identity headers, real payload, epoch validation, incomplete MQTT and 401 rejection passed\n";
}
