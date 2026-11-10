#pragma once
#include "lunaredu_device_contract.h"
#include <string>
#include <string_view>

namespace lunaredu::protocol {
bool wire_id(std::string_view value, std::string_view prefix);
bool operation_id(std::string_view value);
std::string json_quote(std::string_view value);
struct MessageHeader {
  std::string_view type, message_id, sent_at, request_id, correlation_id, session_id;
  int binding_epoch=0;
};
bool wire_timestamp(std::string_view value);
bool valid_header(const MessageHeader &h);
// A JSON adapter can fill its owned document without an encode/parse roundtrip.
// This template does not depend on a JSON library or allocate an intermediate body.
template<class Object> bool write_message_header(Object &out,const MessageHeader &h) {
  if(!valid_header(h))return false;
  out["protocol"]=std::string(contract::name);
 out["version"]=1;out["family"]="device";out["profile"]=std::string(contract::profile_for(h.type));
  out["message_id"]=std::string(h.message_id);
  out["type"]=std::string(h.type);
  out["sent_at"]=std::string(h.sent_at);
  if(!h.request_id.empty())out["request_id"]=std::string(h.request_id);
  if(!h.correlation_id.empty())out["correlation_id"]=std::string(h.correlation_id);
  if(!h.session_id.empty()) {
    out["session"]["id"]=std::string(h.session_id);
    out["session"]["binding_epoch"]=h.binding_epoch;
  }
  return true;
}
// Payload must already be a validated JSON object supplied by the domain codec.
// Clock, ID generation, JSON parsing, IO and authorization stay with the caller.
std::string encode_message(const MessageHeader &h, std::string_view payload);
std::string device_session_id(std::string_view serial);
}
