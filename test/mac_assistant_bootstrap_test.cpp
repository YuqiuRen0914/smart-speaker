#include "interaction/transport/assistant_activation_client.h"
#include "interaction/transport/assistant_transport.h"
#include "interaction/conversation/assistant_runtime_constants.h"
#include "network/http/network_transport_policy.h"
#include <cassert>
#include <iostream>
using namespace lunaredu::lunaros;
bool lunaredu::lunaros::network_fetch_resource(NetworkFetchRequest r, NetworkResponse *out) {
  assert(r.transport_security==NetworkTransportSecurity::AllowPlaintextLocal);
  assert(network_transport_security_allows(r.url,r.transport_security,true));
  assert(r.url=="http://speaker.local:8765/xiaozhi/ota/");
  assert(r.maximum_response_bytes==16384);
  bool token=false;
  for(const auto &[name,value]:r.headers) {
    if(name=="X-Voice-Token") token=value==std::string(64,'a');
    assert(name!="Authorization" && name!="Device-Token");
  }
  assert(token);
  out->status_code=200;
  out->body=R"({"mqtt":{"endpoint":"mqtt://speaker.local:18883","client_id":"test","username":"speaker","password":"test-local","publish_topic":"feiyu/up","subscribe_topic":"feiyu/down"}})";
  return true;
}
int main() {
  using namespace lunaredu::pet::conversation::internal;
  assert(std::string(kActivationNamespace)=="mac_assistant");
  assert(kAssistantUsesMac && !kAssistantUsesLunarLink);
  XiaozhiActivationRequest request;
  request.ota_url="http://speaker.local:8765/xiaozhi/ota/";
  XiaozhiActivationResult result;
  assert(xiaozhi_activate_device(request,result));
  assert(result.has_mqtt && result.mqtt_username=="speaker");
  std::cout<<"Mac bootstrap: explicit local policy, scoped token, independent namespace and MQTT config passed\n";
}
