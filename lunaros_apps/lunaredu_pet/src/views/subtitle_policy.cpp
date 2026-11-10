#include "lunaredu_pet_subtitle_policy.h"
#include <cstring>
namespace {
bool equals(const char *value, const char *expected) {
  return std::strcmp(value == nullptr ? "" : value, expected) == 0;
}

bool has_text(const char *value) {
  return value != nullptr && value[0] != '\0';
}

}
extern "C" const char *lunaros_home_assistant_badge_text(
    const lunaros_home_assistant_state_t *state) {
  if (state == nullptr) return "网络离线";
  if (state->network_provisioning) return "配网中";
  if (state->network_connecting) return "网络连接中";
  if (!state->network_connected) return "网络离线";
  if (!state->assistant_enabled) return "服务停止";
  if (!state->assistant_service_enabled) return "服务启动中";
  if (state->assistant_suspended) return "助手暂停";
  if (!state->assistant_activation_ready) return "待绑定";
  if (equals(state->session_state, "failed")) return "连接异常";
  if (equals(state->session_state, "listening")) {
    return state->assistant_microphone_enabled ? "聆听中" : "麦克风已关闭";
  }
  if (equals(state->session_state, "thinking") ||
      equals(state->session_state, "processing")) {
    return "思考中";
  }
  if (state->assistant_speaking ||
      equals(state->session_state, "speaking")) {
    return "回答中";
  }
  if (equals(state->session_state, "opening") ||
      !state->assistant_transport_connected) {
    return "连接中";
  }
  return "待机";
}

extern "C" const char *lunaros_home_assistant_subtitle_text(
    const lunaros_home_assistant_state_t *state) {
  if (state == nullptr) return "网络未连接，请检查网络";
  if (state->network_provisioning) return "配网热点已开启，请完成网络设置";
  if (state->network_connecting) return "正在连接 Wi-Fi…";
  if (!state->network_connected) return "网络未连接，请检查网络";
  if (!state->assistant_enabled) return "小智已关闭";
  if (!state->assistant_service_enabled) return "小智服务正在启动…";
  if (state->assistant_suspended) return "语音助手暂被其他应用占用";
  if (!state->assistant_activation_ready) {
    return state->assistant_activation_pending ? "小智正在准备连接…"
                                               : "小智尚未绑定";
  }
  if (equals(state->session_state, "failed")) {
    return "小智连接异常，请检查网络";
  }
  if (state->connection_notice_due &&
      (equals(state->session_state, "opening") ||
       !state->assistant_transport_connected)) {
    return !state->assistant_transport_connected ? "小智服务重连中…"
                                                  : "正在准备对话…";
  }
  if (!state->subtitles_enabled && !state->full_screen_pet) return "";

  const bool transcript_available = has_text(state->transcript_text);
  const bool user_transcript = equals(state->transcript_role, "user");
  const bool assistant_transcript =
      equals(state->transcript_role, "assistant");
  if (equals(state->session_state, "listening")) {
    if (!state->assistant_microphone_enabled) return "麦克风已关闭";
    return transcript_available && user_transcript ? state->transcript_text
                                                   : "正在聆听…";
  }
  if (state->assistant_speaking ||
      equals(state->session_state, "speaking")) {
    return transcript_available && assistant_transcript
               ? state->transcript_text
               : "正在回答…";
  }
  if (equals(state->session_state, "opening")) {
    return transcript_available && user_transcript && state->transcript_hold
               ? state->transcript_text
               : "";
  }
  if (equals(state->session_state, "thinking") ||
      equals(state->session_state, "processing")) {
    return transcript_available && user_transcript && state->transcript_hold
               ? state->transcript_text
               : "正在思考…";
  }
  if (state->reply_hold) {
    return transcript_available && assistant_transcript
               ? state->transcript_text
               : "正在回答…";
  }
  if (transcript_available && state->transcript_hold) {
    return state->transcript_text;
  }
  return state->desktop_page ? "说“你好小智”开始对话" : "";
}

extern "C" bool lunaros_interaction_should_interrupt_assistant_on_press(
    bool assistant_speaking) {
  return assistant_speaking;
}

