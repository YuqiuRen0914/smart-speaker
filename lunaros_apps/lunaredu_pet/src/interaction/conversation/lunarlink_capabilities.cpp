#include "interaction/conversation/lunarlink_capabilities.h"
#include "interaction/conversation/assistant_core.h"
#include "interaction/tools/assistant_tool_execution.h"
#include <array>

namespace lunaredu::pet::conversation::internal {
namespace {
struct Receipt {std::string key, signature, type, payload; bool running=false;};
struct Receipts {
  std::string session;
  int epoch=0;
  std::array<Receipt,16> slots;
} receipts;
void reply(const std::string &type, const std::string &request,
           const std::string &correlation, JsonDocument &p) {
  auto &t=g_runtime.transport;
  if (!g_runtime.pending_mcp_messages.try_push(t.wire.envelope(type,t.session_id,
          p.as<JsonVariantConst>(),request,correlation)))
    g_runtime.diagnostics.last_error="lunarlink_capability_reply_queue_full";
}
bool bounded_id(const std::string &s) {return !s.empty() && s.size()<=128;}
}
void handle_lunarlink_capability_locked(JsonDocument &doc, std::unique_lock<std::mutex> &lock) {
  const std::string type=doc["type"]|"";
  const std::string request=doc["request_id"]|"";
  const std::string correlation=doc["correlation_id"]|"";
  if(!bounded_id(request)||correlation.size()>128)return;
  JsonDocument result;
  if(type=="capability.list") {
    auto catalog=result["capabilities"].to<JsonArray>();
    for(const auto &tool:g_runtime.external_tools) {
      if(!tool.handler)continue;
      auto c=catalog.add<JsonObject>();c["id"]=tool.name;c["version"]=1;c["available"]=true;
      c["documentation"]["summary"]=tool.description;
      auto op=c["operations"].to<JsonArray>().add<JsonObject>();op["id"]="call";op["kind"]="command";
      JsonDocument schema;
      if(deserializeJson(schema,tool.input_schema_json)==DeserializationError::Ok)op["arguments"]=schema.as<JsonVariantConst>();
    }
    reply("capability.result",request,correlation,result);return;
  }
  const std::string activity=doc["payload"]["activity_id"]|"";
  const std::string capability=doc["payload"]["capability_id"]|"";
  const std::string operation=doc["payload"]["operation_id"]|"";
  const std::string key=doc["payload"]["idempotency_key"]|"";
  result["activity_id"]=activity;
  auto reject=[&](const char *code){
    result["state"]="rejected";result["error"]["code"]=code;
    result["error"]["message"]="Device action was not admitted";
    reply("activity.rejected",request,correlation,result);
  };
  if(!bounded_id(activity)||!bounded_id(capability)||!bounded_id(key)||operation!="call"||
      !doc["payload"]["arguments"].is<JsonObject>()) {reject("capability.invalid_request");return;}
  std::string args;serializeJson(doc["payload"]["arguments"],args);
  if(args.size()>8192){reject("capability.arguments_too_large");return;}
  const auto session=g_runtime.transport.session_id;
  const int epoch=g_runtime.transport.wire.binding_epoch;
  if(receipts.session!=session||receipts.epoch!=epoch) {receipts={};receipts.session=session;receipts.epoch=epoch;}
  const std::string signature=capability+"\n"+args;
  Receipt *slot=nullptr;
  for(auto &r:receipts.slots) {
    if(r.key==key) {
      if(r.signature!=signature){reject("capability.idempotency_conflict");return;}
      if(r.running){reject("capability.in_progress");return;}
      if(deserializeJson(result,r.payload)!=DeserializationError::Ok)return;
      result["activity_id"]=activity;reply(r.type,request,correlation,result);return;
    }
    if(!slot&&r.key.empty())slot=&r;
  }
  if(!slot){reject("capability.receipt_capacity");return;}
  ToolCallHandler handler=nullptr;void *context=nullptr;
  for(const auto &t:g_runtime.external_tools)if(t.name==capability){handler=t.handler;context=t.context;break;}
  if(!handler){reject("capability.unavailable");return;}
  // Admission reserves reply room before running the existing product handler.
  // Never evict a completed write receipt within the live session.
  slot->key=key;slot->signature=signature;slot->running=true;
  std::string response,error;bool ok=false;
  const uint32_t generation=g_runtime.platform_generation;
  const auto disposition=execute_unlocked_tool(g_runtime.pending_mcp_messages,lock,[&]{
    ok=handler(context,args.c_str(),response,error);
  });
  if(disposition==ToolExecutionDisposition::SessionChanged || generation!=g_runtime.platform_generation ||
      session!=g_runtime.transport.session_id || !(g_runtime.platform_identity==lunarlink_identity()))return;
  if(disposition==ToolExecutionDisposition::QueueBusy){*slot={};reject("capability.reply_queue_busy");return;}
  result["state"]=ok?"succeeded":"failed";
  result["result"]["completion"]=ok?"completed":"unknown";
  if(!ok){result["error"]["code"]="capability.execution_failed";result["error"]["message"]=error.substr(0,1024);}
  if(response.size()<=16384) {
    JsonDocument output;
    if(deserializeJson(output,response)==DeserializationError::Ok)result["result"]["output"]=output.as<JsonVariantConst>();
    else result["result"]["output_text"]=response;
  } else result["result"]["truncated"]=true;
  slot->type=ok?"activity.completed":"activity.failed";slot->running=false;
  serializeJson(result,slot->payload);
  reply(slot->type,request,correlation,result);
}
}
