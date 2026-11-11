#include "interaction/transport/assistant_activation_client.h"
#include "interaction/transport/assistant_transport.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
using namespace lunaredu::lunaros;
int phase=0, calls=0;
bool lunaredu::lunaros::network_fetch_resource(NetworkFetchRequest r, NetworkResponse *out) {
 ++calls;
 assert(r.maximum_response_bytes==16384);
 bool device=false, activation=false;
 for(const auto &[name,value]:r.headers){
  assert(name!="Device-Token" && name!="Authorization" && name!="X-Lunaredu-Protocol");
  if(name=="Device-Id")device=value=="00:11:22:33:44:55";
  if(name=="Activation-Version")activation=value=="1";
 }
 assert(device && activation);
 JsonDocument body;assert(!deserializeJson(body,r.body));
 if(r.url.ends_with("/activate")){
  assert(body["challenge"]=="test-challenge");
  out->status_code=phase==0?202:200;out->body="{}";
 }else{
  assert(body["version"]==2 && body["mac_address"]=="00:11:22:33:44:55");
  assert(body["hardware_serial"].isNull());
  out->status_code=phase==2?401:200;
  out->body=phase==0?R"({"activation":{"code":"123456","challenge":"test-challenge"}})":
    R"({"mqtt":{"endpoint":"broker.example","client_id":"test","publish_topic":"up","subscribe_topic":"down"}})";
 }
 return true;
}
int main(){
 XiaozhiActivationRequest request;request.ota_url="https://api.tenclass.net/xiaozhi/ota/";
 request.device_id="00:11:22:33:44:55";request.client_id="test-uuid";
 // A Board account/token is deliberately absent: XiaoZhi activation is independent.
 XiaozhiActivationResult result;
 assert(xiaozhi_activate_device(request,result));
 assert(result.activation_pending && result.activation_code=="123456" && calls==2);
 phase=1;result={};assert(xiaozhi_activate_device(request,result));
 assert(result.has_mqtt && !result.activation_pending);
 phase=2;result={};assert(!xiaozhi_activate_device(request,result));
 assert(result.error=="activation_bad_status_401");
 std::cout<<"XiaoZhi activation: independent identity, challenge polling, MQTT success and HTTP rejection passed\n";
}
