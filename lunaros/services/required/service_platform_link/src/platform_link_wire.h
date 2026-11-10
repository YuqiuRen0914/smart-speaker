#pragma once
#include "lunaredu_device_wire_envelope.h"
#include <ArduinoJson.h>
#include <ctime>
#include "lunaredu_operation_result.h"

namespace lunaredu::lunaros::platform_link::wire {
inline const char *text(JsonVariantConst value) { return value.is<const char *>()?value.as<const char *>():""; }
inline protocol::MessageHeader header(JsonObjectConst value) {
  return {text(value["type"]),text(value["message_id"]),text(value["sent_at"]),
    text(value["request_id"]),text(value["correlation_id"]),text(value["session"]["id"]),value["session"]["binding_epoch"]| -1};
}
// Only the declared fixed-work installer gets a larger durable document.
inline size_t request_limit(std::string_view module,std::string_view operation) {
  return module=="sequence"&&operation=="install"?protocol::contract::operation_max_sequence_install_request_bytes:protocol::contract::operation_max_request_bytes;
}
inline bool operation(JsonObjectConst frame, const char *delivery) {
  const auto h=header(frame);
  for(const auto field:frame) {
    const std::string_view key=field.key().c_str();
    if(key!="protocol"&&key!="version"&&key!="family"&&key!="profile"&&key!="message_id"&&key!="request_id"&&key!="correlation_id"&&key!="type"&&
        key!="sent_at"&&key!="sequence"&&key!="session"&&key!="payload")return false;
  }
  for(const auto field:frame["session"].as<JsonObjectConst>())
    if(std::string_view(field.key().c_str())!="id"&&std::string_view(field.key().c_str())!="binding_epoch")return false;
  for(const auto field:frame["payload"].as<JsonObjectConst>()) {
    const std::string_view key=field.key().c_str();
    if(key!="module_id"&&key!="operation_id"&&key!="request"&&key!="delivery"&&key!="expires_at"&&key!="authority")return false;
  }
  const auto payload=frame["payload"].as<JsonObjectConst>();
  if(!frame["sequence"].isNull()&&!frame["sequence"].is<uint64_t>())return false;
  if(std::string_view(delivery)=="durable") {
    const std::string_view expiry=text(payload["expires_at"]);
    if(!protocol::wire_timestamp(expiry)||expiry.back()!='Z'||!payload["authority"].isNull())return false;
  } else if(std::string_view(delivery)=="realtime") {
    if(!payload["expires_at"].isNull()||!payload["authority"].is<JsonObjectConst>())return false;
    const auto authority=payload["authority"].as<JsonObjectConst>();
    for(const auto field:authority) {
      const std::string_view key=field.key().c_str();
      if(key!="access_epoch"&&key!="shared"&&key!="shared_control_enabled"&&key!="control_manager_id")return false;
    }
    if(!authority["access_epoch"].is<int>()||authority["access_epoch"].as<int>()<0||
        !authority["shared"].is<bool>()||!authority["shared_control_enabled"].is<bool>()||
        !authority["control_manager_id"].is<const char *>()||std::string_view(text(authority["control_manager_id"])).size()>64)return false;
  } else return false;
  return frame["protocol"]==protocol::contract::name.data() && frame["version"].is<int>() && frame["version"].as<int>()==1 && frame["family"]=="device" && frame["profile"]=="operation" &&
    protocol::valid_header(h) && h.type==protocol::contract::operation_invoke &&
    protocol::wire_id(h.request_id,"req") && !h.session_id.empty() &&
    frame["session"]["binding_epoch"].is<int>() && h.binding_epoch>0 &&
    frame["payload"].is<JsonObjectConst>() && frame["payload"]["delivery"]==delivery &&
    protocol::operation_id(text(frame["payload"]["module_id"])) && protocol::operation_id(text(frame["payload"]["operation_id"])) &&
    frame["payload"]["request"].is<JsonObjectConst>() && measureJson(frame["payload"]["request"])<=request_limit(text(payload["module_id"]),text(payload["operation_id"]));
}
struct RealtimeAccess {
  int channel_epoch=0,binding_epoch=0,access_epoch=0;
  bool bound=false,revocation_requested=false,persistence_pending=false,shared_control_enabled=false;
};
inline bool realtime_authorized(JsonObjectConst frame,std::string_view serial,const RealtimeAccess &access) {
  const auto authority=frame["payload"]["authority"].as<JsonObjectConst>();
  return operation(frame,"realtime") && frame["session"]["id"]==protocol::device_session_id(serial) &&
      authority["access_epoch"].is<int>() && authority["access_epoch"].as<int>()>=0 &&
      authority["shared"].is<bool>() && authority["shared_control_enabled"].is<bool>() &&
      frame["session"]["binding_epoch"].as<int>()==access.channel_epoch &&
      access.binding_epoch==access.channel_epoch && access.bound &&
      !access.revocation_requested && !access.persistence_pending &&
      authority["access_epoch"].as<int>()>=access.access_epoch &&
      (!authority["shared"].as<bool>() || (access.shared_control_enabled &&
       authority["access_epoch"].as<int>()==access.access_epoch));
}
inline bool deadline_live(JsonVariantConst value) {
  const std::string_view stamp=text(value);
  if (!protocol::wire_timestamp(stamp)||stamp.back()!='Z') return false;
  const auto now=std::time(nullptr);
  if(now<1704067200)return false;
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc,&now);
#else
  gmtime_r(&now,&utc);
#endif
  char current[32]{};std::strftime(current,sizeof(current),"%Y-%m-%dT%H:%M:%SZ",&utc);
  // Conservative to one second: an expired movement/management command never starts late.
  return stamp.substr(0,19)>std::string_view(current,19);
}
inline void result(JsonDocument &out, JsonObjectConst request, bool ok,
                   JsonObjectConst data, const std::string &error) {
  const bool too_large=measureJson(data)>protocol::contract::operation_max_result_bytes;
  if (too_large) ok=false;
  const auto input=header(request);
  const std::string id="msg_"+std::string(input.request_id.substr(4));
  const auto now=std::time(nullptr);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc,&now);
#else
  gmtime_r(&now,&utc);
#endif
  char stamp[32]{};
  std::strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",&utc);
  out.clear();
  if(!protocol::write_message_header(out,{protocol::contract::operation_result,id,stamp,
    input.request_id,input.message_id,input.session_id,input.binding_epoch}))return;
  out["payload"]["ok"]=ok;
  if (!too_large) out["payload"]["result"].set(data);
  if (!out["payload"]["result"].is<JsonObject>()) out["payload"]["result"].to<JsonObject>();
  if (!ok) protocol::write_operation_error(out["payload"].as<JsonObject>(),too_large?"operation_result_too_large":error);
}
}
