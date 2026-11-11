#include "interaction/transport/lunarlink_protocol.h"
#include <atomic>
#include <cstdio>
#include <ctime>
#include <esp_timer.h>

namespace {
std::atomic<uint32_t> sequence{0};
const char *profile(const std::string &type) {
  return type.rfind("media.", 0) == 0 ? "media" : "activity";
}
std::string serialize(JsonVariantConst doc) {
  std::string out; serializeJson(doc, out); return out;
}
}
std::string lunarlink_next_id(const char *prefix) {
  char id[80];
  std::snprintf(id, sizeof(id), "%s_%llx-%lx", prefix,
      static_cast<unsigned long long>(esp_timer_get_time()),
      static_cast<unsigned long>(++sequence));
  return id;
}
bool lunarlink_header(JsonVariantConst doc) {
  const std::string type = doc["type"] | "";
  const std::string id = doc["message_id"] | "";
  const std::string timestamp = doc["sent_at"] | "";
  return doc["protocol"] == "lunaredu.wire" && doc["family"] == "device" &&
      doc["version"].is<int>() && doc["version"].as<int>() == 1 &&
      doc["profile"] == profile(type) && !type.empty() &&
      id.rfind("msg_", 0) == 0 && id.size() <= 128 &&
      timestamp.size() >= 20 && timestamp.size() <= 32 &&
      doc["payload"].is<JsonObjectConst>();
}
std::string LunarLinkProtocol::envelope(const std::string &type, const std::string &session,
    JsonVariantConst payload, const std::string &request, const std::string &correlation) const {
  JsonDocument out;
  out["protocol"] = "lunaredu.wire"; out["family"] = "device";
  out["version"] = 1; out["profile"] = profile(type);
  out["type"] = type; out["message_id"] = lunarlink_next_id("msg");
  const auto now = std::time(nullptr); std::tm utc{}; gmtime_r(&now, &utc);
  char timestamp[32]{}; std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
  out["sent_at"] = timestamp;
  if (!request.empty()) out["request_id"] = request;
  if (!correlation.empty()) out["correlation_id"] = correlation;
  if (!session.empty()) {out["session"]["id"] = session; out["session"]["binding_epoch"] = binding_epoch;}
  out["payload"] = payload;
  return serialize(out.as<JsonVariantConst>());
}
std::string LunarLinkProtocol::open() {
  open_request = lunarlink_next_id("req"); activity_id.clear(); session_opened = false;
  JsonDocument p;
  p["device_id"] = device_id; p["binding_epoch"] = binding_epoch;
  auto features = p["features"].to<JsonArray>();
  for (const auto *f : {"activity.feedback", "device.capability.execute",
                       "media.audio", "media.audio.aes_gcm", "conversation.playback_ready",
                       "conversation.playback_receipt"}) features.add(f);
  auto offer = p["media_offers"].to<JsonArray>().add<JsonObject>();
  offer["kind"] = "audio"; offer["direction"] = "device_to_platform";
  offer["encoding"] = "opus"; offer["sample_rate_hz"] = 16000;
  offer["channels"] = 1; offer["frame_duration_ms"] = 20;
  return envelope("session.open", {}, p.as<JsonVariantConst>(), open_request);
}
std::string LunarLinkProtocol::encode_intent(const std::string &intent, uint32_t last_uplink) {
  JsonDocument in;
  if (deserializeJson(in, intent) != DeserializationError::Ok) return {};
  const std::string session = in["session_id"] | "";
  const std::string type = in["type"] | "";
  if (lunarlink_header(in.as<JsonVariantConst>())) {
    return intent;
  }
  if (session.empty() || binding_epoch <= 0) return {};
  const std::string state = in["state"] | "";
  JsonDocument p;
  std::string wire_type;
  if (type == "listen" && (state == "start" || state == "detect")) {
    activity_id = lunarlink_next_id("act");
    first_downlink = expected_last_downlink = played_packets = last_played_sequence = 0;
    playback_failed = terminal_seen = receipt_sent = false;
    wire_type = "activity.submit";
    p["capability_id"] = "conversation.session"; p["operation_id"] = "start";
    p["idempotency_key"] = activity_id;
    p["arguments"]["mode"] = in["mode"] | "manual";
    p["arguments"]["language"] = "zh-CN";
    p["arguments"]["first_sequence"] = last_uplink + 1;
    if (state == "detect") p["arguments"]["text"] = in["text"] | "";
  } else if (type == "listen" && state == "stop") {
    if (activity_id.empty()) return {};
    wire_type = "media.close"; p["reason"] = "end_of_speech"; p["last_sequence"] = last_uplink;
  } else if (type == "abort") {
    if (activity_id.empty()) return {};
    wire_type = "activity.cancel"; p["reason"] = "user_cancel";
  } else return {};
  p["activity_id"] = activity_id;
  auto result = envelope(wire_type, session, p.as<JsonVariantConst>(), {}, activity_id);
  if (type == "abort") activity_id.clear();
  return result;
}
bool LunarLinkProtocol::decode(const std::string &message, const std::string &session,
    JsonDocument &out, std::string &ready_receipt) {
  out.clear(); ready_receipt.clear();
  JsonDocument in;
  if (message.size() > 32U * 1024U || deserializeJson(in, message) != DeserializationError::Ok ||
      !lunarlink_header(in.as<JsonVariantConst>())) return false;
  const std::string type = in["type"] | "";
  JsonObjectConst p = in["payload"].as<JsonObjectConst>();
  if (type == "protocol.error" && !session_opened && !open_request.empty() &&
      in["request_id"] == open_request && in["session"].isNull()) {
    out["type"] = "lunarlink_failure";
    out["text"] = p["error"]["message"] | "LunarLink session rejected";
    return true;
  }
  if (type == "session.opened") {
    if (session_opened || open_request.empty() || in["request_id"] != open_request ||
        p["binding_epoch"] != binding_epoch) return false;
    const std::string new_session = p["session_id"] | "";
    if (new_session.empty() || new_session.size() > 128 || (!session.empty() && session != new_session)) return false;
    auto media = p["media"]; auto input = p["input_media"];
    if (media["encoding"] != "opus" || media["sample_rate_hz"] != 24000 ||
        media["channels"] != 1 || media["frame_duration_ms"] != 20 ||
        input["encoding"] != "opus" || input["sample_rate_hz"] != 16000 ||
        input["channels"] != 1 || input["frame_duration_ms"] != 20 ||
        p["endpoint"]["encryption"] != "aes-128-gcm") return false;
    session_opened = true;
    playback_ready = false;
    for (auto f : p["features"].as<JsonArrayConst>()) if (f == "conversation.playback_ready") playback_ready = true;
    out["type"] = "hello"; out["transport"] = "udp"; out["session_id"] = new_session;
    out["audio_params"]["sample_rate"] = 24000; out["audio_params"]["channels"] = 1;
    out["audio_params"]["frame_duration"] = 20;
    out["udp"]["server"] = p["endpoint"]["host"];
    out["udp"]["port"] = p["endpoint"]["port"];
    out["udp"]["key"] = p["endpoint"]["key"];
    out["udp"]["nonce"] = p["endpoint"]["nonce"];
    return true;
  }
  if (session.empty() || in["session"]["id"] != session || in["session"]["binding_epoch"] != binding_epoch) return false;
  if (type == "session.closed") {
    out["type"] = "goodbye"; out["session_id"] = session;
    return true;
  }
  if (type == "protocol.error") {
    out["type"] = "lunarlink_failure";
    out["text"] = p["error"]["message"] | "LunarLink protocol error";
    return true;
  }
  if (type == "capability.list" || type == "activity.submit") {
    out.set(in); return true;
  }
  if (activity_id.empty() || p["activity_id"] != activity_id) return false;
  out["session_id"] = session;
  if (type == "activity.progress") {
    if (terminal_seen) return false;
    const std::string phase = p["phase"] | "";
    if (phase == "recognized") {out["type"] = "stt"; out["text"] = p["message"] | "";}
    else if (phase == "response") {out["type"] = "tts"; out["state"] = "sentence_start"; out["text"] = p["message"] | "";}
    else if (phase == "emotion") {out["type"] = "llm"; out["emotion"] = p["message"] | "";}
    else if (phase == "speaking" || phase == "playback_preparing") {
      const uint32_t first = p["result"]["first_audio_sequence"] | 0U;
      if (first == 0) return false;
      if (!first_downlink) first_downlink = first;
      if (first_downlink != first) return false;
      out["type"] = "tts"; out["state"] = "start";
      if (playback_ready) {
        JsonDocument ready; ready["event_type"] = "conversation.playback_ready";
        ready["activity_id"] = activity_id; ready["first_sequence"] = first_downlink;
        ready_receipt = envelope("event.emit", session, ready.as<JsonVariantConst>(), {}, activity_id);
      }
    } else return false;
  } else if (type == "activity.completed") {
    if (terminal_seen) return false;
    terminal_seen = true;
    expected_last_downlink = p["result"]["last_audio_sequence"] | 0U;
    out["type"] = "tts"; out["state"] = "stop";
  } else if (type == "activity.failed" || type == "activity.canceled" || type == "activity.rejected") {
    out["type"] = "lunarlink_failure"; out["text"] = p["error"]["message"] | "LunarLink activity failed";
    activity_id.clear();
  } else return false;
  return true;
}
std::string LunarLinkProtocol::playback_receipt(const std::string &session, bool delivered) const {
  JsonDocument p;
  p["event_type"] = "conversation.playback_completed"; p["activity_id"] = activity_id;
  p["last_sequence"] = last_played_sequence; p["delivered"] = delivered;
  return envelope("event.emit", session, p.as<JsonVariantConst>(), {}, activity_id);
}

bool LunarLinkProtocol::playback_delivered(bool received, bool output_error, bool binary_error) const {
  return received && !playback_failed && !output_error && !binary_error &&
      first_downlink > 0 && expected_last_downlink >= first_downlink &&
      last_played_sequence == expected_last_downlink &&
      played_packets == expected_last_downlink - first_downlink + 1;
}
