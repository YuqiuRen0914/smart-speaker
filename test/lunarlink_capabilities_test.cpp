#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/lunarlink_capabilities.h"
#include <cassert>
#include <iostream>
using namespace lunaredu::pet::conversation::internal;
HostRuntime lunaredu::pet::conversation::internal::g_runtime;
HostIdentity lunaredu::pet::conversation::internal::lunarlink_identity(){return g_runtime.platform_identity;}
int calls=0;bool fail=false,change_session=false;
bool handler(void*,const char *args,std::string &response,std::string &error){
 assert(g_runtime.mutex.try_lock());g_runtime.mutex.unlock();
 assert(std::string(args)=="{\"volume\":40}");++calls;
 if(change_session){++g_runtime.platform_generation;g_runtime.transport.session_id="ses_rebound";g_runtime.pending_mcp_messages.clear();}
 if(fail){error="write_failed";return false;}
 response=R"({"observed_volume":40})";return true;
}
JsonDocument request(const char *key="key_test"){
 JsonDocument d;d["type"]="activity.submit";d["request_id"]="req_test";d["correlation_id"]="act_test";
 d["payload"]["activity_id"]="act_tool";d["payload"]["capability_id"]="volume";
 d["payload"]["operation_id"]="call";d["payload"]["idempotency_key"]=key;d["payload"]["arguments"]["volume"]=40;return d;
}
JsonDocument call(JsonDocument &d){std::unique_lock lock(g_runtime.mutex);handle_lunarlink_capability_locked(d,lock);JsonDocument result;
 const auto reply=g_runtime.pending_mcp_messages.pop();if(!reply.empty())assert(!deserializeJson(result,reply));return result;}
int main(){
 g_runtime.transport.wire.binding_epoch=1;
 g_runtime.external_tools.push_back({"volume","Set volume",R"({"type":"object"})",handler,nullptr});
 auto d=request();auto r=call(d);assert(calls==1&&r["type"]=="activity.completed"&&r["payload"]["result"]["output"]["observed_volume"]==40);
 d["payload"]["activity_id"]="act_retry";r=call(d);assert(calls==1&&r["payload"]["activity_id"]=="act_retry");
 d["payload"]["arguments"]["volume"]=50;r=call(d);assert(calls==1&&r["type"]=="activity.rejected"&&r["payload"]["error"]["code"]=="capability.idempotency_conflict");
 d=request("key_failed");fail=true;r=call(d);assert(calls==2&&r["type"]=="activity.failed"&&r["payload"]["result"]["completion"]=="unknown");
 r=call(d);assert(calls==2&&r["type"]=="activity.failed");fail=false;
 d=request("key_busy");for(size_t i=0;i<PendingMcpMessages::kCapacity;++i)assert(g_runtime.pending_mcp_messages.try_push("busy"));
 {std::unique_lock lock(g_runtime.mutex);handle_lunarlink_capability_locked(d,lock);}
 assert(calls==2);g_runtime.pending_mcp_messages.clear();r=call(d);assert(calls==3&&r["type"]=="activity.completed");
 d=request("key_withdrawn");change_session=true;r=call(d);assert(calls==4&&r.isNull());change_session=false;
 d=request("key_test");r=call(d);assert(calls==5&&r["type"]=="activity.completed"&&r["session"]["id"]=="ses_rebound");
 JsonDocument catalog;catalog["type"]="capability.list";catalog["request_id"]="req_catalog";r=call(catalog);assert(r["type"]=="capability.result"&&r["payload"]["capabilities"][0]["id"]=="volume");
 for(int i=1;i<=16;++i){auto key="key_fill_"+std::to_string(i);d=request(key.c_str());r=call(d);}
 assert(calls==20&&r["type"]=="activity.rejected"&&r["payload"]["error"]["code"]=="capability.receipt_capacity");
 std::cout<<"LunarLink native capabilities: real handlers unlocked, idempotent writes, conflicts/failure receipts, queue pressure and rebind cancellation passed\n";
}
