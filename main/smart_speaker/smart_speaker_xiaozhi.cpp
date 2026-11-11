#include "smart_speaker_xiaozhi.h"

#include <algorithm>
#include <string>

#include <ArduinoJson.h>

#include "lunaredu_audio_bridge.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/persistence/system_audio.h"
#include "lunaredu_pet_conversation.h"
#include "lunaredu_pet_conversation_entry.h"
#include "lunaredu_pet_conversation_policy.h"
#include "lunaredu_pet_emotion.h"
#include "smart_speaker_audio.h"
#include "smart_speaker_display.h"
#include "smart_speaker_runtime.h"
#include "smart_speaker_tool_status.h"

namespace smart_speaker {
namespace {

constexpr const char *kTag = "speaker_xiaozhi";

bool audio_enable_input(void *, bool enable) {
  return smart_speaker_voice_audio_enable_input(enable);
}

bool audio_enable_output(void *, bool enable) {
  return smart_speaker_voice_audio_enable_output(enable);
}

bool audio_configure_output(void *, uint32_t sample_rate) {
  (void)sample_rate;
  return smart_speaker_voice_audio_configure_output(sample_rate);
}

bool audio_read_input(void *, int16_t *samples, size_t sample_count,
                      uint32_t timeout_ms) {
  return smart_speaker_voice_audio_read_input(samples, sample_count,
                                              timeout_ms);
}

bool audio_write_output(void *, const int16_t *samples, size_t sample_count,
                        uint32_t timeout_ms) {
  return smart_speaker_voice_audio_write_output(samples, sample_count,
                                                timeout_ms);
}

bool control_allowed(void *) { return true; }

bool request_assistant_audio_focus(void *, const char *, uint32_t timeout_ms) {
  const auto policy = lunaredu::pet::conversation::assistant_policy_snapshot();
  if (!policy.desired_enabled || !policy.microphone_enabled ||
      !policy.audio_focus_owner.empty()) return false;
  return assistant_playback_focus_acquire(timeout_ms);
}

void release_assistant_audio_focus(void *, const char *) {
  assistant_playback_focus_release();
}

void assistant_visual_event(void *,
    const lunaredu::pet::conversation::AssistantEvent &event) {
  if (event.kind != lunaredu::pet::conversation::AssistantEventKind::Emotion ||
      event.value == nullptr || event.value[0] == '\0') return;
  lunaredu::presentation::Expression expression;
  if (!lunaredu::pet::try_pet_expression_from_emotion(event.value, expression)) return;
  AssistantVisualState state = AssistantVisualState::Inactive;
  switch (expression) {
    case lunaredu::presentation::Expression::Happy:
    case lunaredu::presentation::Expression::Celebrate:
      state = AssistantVisualState::Happy; break;
    case lunaredu::presentation::Expression::Curious:
      state = AssistantVisualState::Curious; break;
    case lunaredu::presentation::Expression::Sleepy:
      state = AssistantVisualState::Sleepy; break;
    case lunaredu::presentation::Expression::Error:
      state = AssistantVisualState::Error; break;
    default:
      return;
  }
  smart_speaker_display_set_assistant_emotion(state);
}

bool respond_ok(std::string text, std::string &response, std::string &error) {
  response = std::move(text);
  error.clear();
  return true;
}

bool tool_play_pause(void *, const char *, std::string &response,
                     std::string &error) {
  const auto status = status_snapshot();
  if (status.playing && !status.paused) {
    if (!pause()) { error = "暂停命令队列繁忙"; return false; }
    return respond_ok("已提交暂停请求，设备状态待确认", response, error);
  }
  if (!play()) { error = "播放命令队列繁忙"; return false; }
  return respond_ok("已提交播放请求，设备状态待确认", response, error);
}

bool tool_next(void *, const char *, std::string &response,
               std::string &error) {
  if (!next()) { error = "切歌命令队列繁忙"; return false; }
  return respond_ok("已提交下一首请求，设备状态待确认", response, error);
}

bool tool_previous(void *, const char *, std::string &response,
                   std::string &error) {
  if (!previous()) { error = "切歌命令队列繁忙"; return false; }
  return respond_ok("已提交上一首请求，设备状态待确认", response, error);
}

bool tool_stop(void *, const char *, std::string &response,
               std::string &error) {
  if (!stop()) { error = "停止命令队列繁忙"; return false; }
  return respond_ok("已提交停止请求，设备状态待确认", response, error);
}

bool tool_set_volume(void *, const char *arguments, std::string &response,
                     std::string &error) {
  JsonDocument doc;
  if (deserializeJson(doc, arguments) != DeserializationError::Ok) {
    error = "音量参数格式不正确";
    return false;
  }
  if (!doc["volume"].is<int>()) {
    error = "缺少 volume";
    return false;
  }
  const int volume = std::clamp(doc["volume"].as<int>(), 0, 70);
  if (!set_volume(volume)) { error = "音量命令队列繁忙"; return false; }
  response = "已提交音量设置请求（" + std::to_string(volume) + "），设备状态待确认";
  error.clear();
  return true;
}

bool tool_status(void *, const char *, std::string &response,
                 std::string &error) {
  const auto status = status_snapshot();
  JsonDocument doc;
  fill_playback_tool_status(doc.to<JsonObject>(),
      {status.audio_ready, status.playing, status.paused, status.uploading,
       status.volume, status.current_name, status.playlist_count,
       status.last_error});
  serializeJson(doc, response);
  error.clear();
  return true;
}

bool tool_play_track(void *, const char *arguments, std::string &response,
                     std::string &error) {
  JsonDocument doc;
  if (deserializeJson(doc, arguments) != DeserializationError::Ok) {
    error = "点歌参数格式不正确";
    return false;
  }
  const auto tracks = playlist_snapshot();
  std::string path;
  if (doc["index"].is<int>()) {
    const int index = doc["index"].as<int>();
    if (index >= 0 && static_cast<size_t>(index) < tracks.size()) {
      path = tracks[static_cast<size_t>(index)].path;
    }
  }
  if (path.empty()) {
    const std::string query = doc["name"] | "";
    auto found = std::find_if(tracks.begin(), tracks.end(), [&](const auto &t) {
      return !query.empty() && t.name.find(query) != std::string::npos;
    });
    if (found != tracks.end()) path = found->path;
  }
  if (path.empty()) {
    error = tracks.empty() ? "歌曲库为空" : "没有找到这首歌";
    return false;
  }
  JsonDocument request;
  request["path"] = path;
  JsonDocument output;
  const auto result = handle_track_play_payload(request.as<JsonVariantConst>(),
                                                output.to<JsonObject>());
  if (result.status_code >= 400) {
    error = result.error.empty() ? "点歌失败" : "点歌失败：" + result.error;
    return false;
  }
  response = "已提交歌曲播放请求，设备状态待确认";
  error.clear();
  return true;
}

void register_tools() {
  using lunaredu::pet::conversation::ToolRegistration;
  const ToolRegistration tools[] = {
      {"speaker.play_pause", "播放或暂停本地音乐。", R"json({"type":"object","additionalProperties":false,"properties":{}})json", tool_play_pause, nullptr},
      {"speaker.next", "切到下一首本地歌曲。", R"json({"type":"object","additionalProperties":false,"properties":{}})json", tool_next, nullptr},
      {"speaker.previous", "切到上一首本地歌曲。", R"json({"type":"object","additionalProperties":false,"properties":{}})json", tool_previous, nullptr},
      {"speaker.stop", "停止本地音乐播放。", R"json({"type":"object","additionalProperties":false,"properties":{}})json", tool_stop, nullptr},
      {"speaker.set_volume", "设置音箱音量，volume 范围 0 到 70。", R"json({"type":"object","additionalProperties":false,"properties":{"volume":{"type":"integer","minimum":0,"maximum":70}},"required":["volume"]})json", tool_set_volume, nullptr},
      {"speaker.status", "查询当前播放状态、设备最近错误和上传占用；可用于复查已提交命令的设备状态，但最近错误不是逐命令回执。", R"json({"type":"object","additionalProperties":false,"properties":{}})json", tool_status, nullptr},
      {"speaker.play_track", "按歌曲索引或名称播放本地歌曲。", R"json({"type":"object","additionalProperties":false,"properties":{"index":{"type":"integer","minimum":0},"name":{"type":"string","minLength":1,"maxLength":96}}})json", tool_play_track, nullptr},
  };
  for (const auto &tool : tools) {
    if (!lunaredu::pet::conversation::register_tool(tool)) {
      LOGW(kTag, "tool registration failed: %s", tool.name.c_str());
    }
  }
}

} // namespace

bool register_xiaozhi_projection(std::string &error) {
  return lunaredu::pet::conversation::register_device_projection_surface(error);
}

bool setup_xiaozhi_service() {
  lunaredu::lunaros::configure_audio_output(
      {lunaredu::audio::local_pcm_audio_bridge_set_output_volume});

  lunaredu::audio::LocalPcmAudioBridgeDriver driver;
  driver.input_sample_rate = smart_speaker_voice_audio_input_sample_rate();
  driver.output_sample_rate = smart_speaker_voice_audio_output_sample_rate();
  driver.enable_input = audio_enable_input;
  driver.enable_output = audio_enable_output;
  driver.configure_output = audio_configure_output;
  driver.read_input = audio_read_input;
  driver.write_output = audio_write_output;
  (void)lunaredu::audio::configure_local_pcm_audio_bridge_driver(driver);

  lunaredu::pet::conversation::setup_assistant_policy();
  lunaredu::pet::conversation::setup_entry_policy();

  lunaredu::pet::conversation::ServiceConfig config;
  config.tools.control_allowed = control_allowed;
  config.enabled = lunaredu::pet::conversation::assistant_desired_enabled();
  config.host.request_audio_focus = request_assistant_audio_focus;
  config.host.release_audio_focus = release_assistant_audio_focus;
  config.host.on_event = assistant_visual_event;
  config.enabled_changed =
      lunaredu::pet::conversation::on_assistant_enabled_changed;
  if (!lunaredu::pet::conversation::setup_service(config)) {
    LOGE(kTag, "xiaozhi service initialization failed");
    return false;
  }
  (void)lunaredu::pet::conversation::set_standby_mode(
      lunaredu::pet::conversation::StandbyMode::WakeWord);
  register_tools();
  return true;
}

void tick_xiaozhi_service() { lunaredu::pet::conversation::tick_service(); }

bool xiaozhi_start_activation() {
  return lunaredu::pet::conversation::request_activation();
}

bool xiaozhi_clear_activation() {
  return lunaredu::pet::conversation::clear_activation();
}

bool xiaozhi_start_listening() {
  return lunaredu::pet::conversation::request_listening();
}

bool xiaozhi_stop_listening() {
  return lunaredu::pet::conversation::cancel_conversation();
}

bool xiaozhi_finish_listening() {
  const auto status = lunaredu::pet::conversation::snapshot();
  if (!status.listening) return true;
  return lunaredu::pet::conversation::finish_listening();
}

std::string xiaozhi_status_label(bool show_spoken_line) {
  const auto status = lunaredu::pet::conversation::snapshot();
  if (!status.initialized) return "助手未初始化";
  if (status.activation_pending) {
    if (!status.activation_code.empty()) {
      return "助手配对码 " + status.activation_code;
    }
    return "助手连接中";
  }
  if (!status.activation_ready) return "请先绑定平台账号";
  if (status.session_state == "thinking") return "助手思考中";
  if (status.listening) return "助手正在听";
  if (status.speaking) {
    if (show_spoken_line && status.transcript_role == "assistant" && !status.transcript_text.empty()) {
      constexpr size_t kMaxCaptionBytes = 96;
      if (status.transcript_text.size() <= kMaxCaptionBytes) return status.transcript_text;
      size_t end = kMaxCaptionBytes;
      while (end > 0 && (static_cast<uint8_t>(status.transcript_text[end]) & 0xc0) == 0x80) --end;
      return status.transcript_text.substr(0, end) + "…";
    }
    return "助手正在说";
  }
  if (status.opening) return "助手连接中";
  if (status.failed) return status.last_error.empty() ? "助手异常" : status.last_error;
  if (status.transport_connected) return "长按呼叫助手";
  if (status.ready) return "助手待命";
  return "助手准备中";
}

} // namespace smart_speaker
