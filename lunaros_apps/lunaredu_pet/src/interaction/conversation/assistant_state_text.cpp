#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/product_system_state.h"

namespace lunaredu::pet::conversation::internal {

namespace {

const char *standby_mode_status_text(StandbyListenMode mode) {
  return mode == StandbyListenMode::ContinuousConversation ? "连续对话"
                                                           : "唤醒词";
}

const char *stop_result_text(const std::string &reason) {
  if (reason.empty()) {
    return "暂无";
  }
  if (reason == "auto_stop") {
    return "自动收句";
  }
  if (reason == "goodbye") {
    return "服务端结束";
  }
  if (reason == "sent") {
    return "已发送停止";
  }
  if (reason == "send_failed") {
    return "发送停止失败";
  }
  if (reason == "noop") {
    return "无需停止";
  }
  return reason.c_str();
}

} // namespace

std::string format_progress_counter_locked() {
  char buffer[96] = {};
  if (g_runtime.session_state == AssistantSessionState::Listening) {
    const uint32_t elapsed_seconds =
        g_runtime.conversation.listening_started_at_ms > 0 &&
                now_ms() > g_runtime.conversation.listening_started_at_ms
            ? static_cast<uint32_t>((now_ms() - g_runtime.conversation.listening_started_at_ms) / 1000U)
            : 0U;
    const uint32_t sent_packets =
        g_runtime.sent_audio_packets >= g_runtime.listening_sent_audio_packets_base
            ? (g_runtime.sent_audio_packets - g_runtime.listening_sent_audio_packets_base)
            : g_runtime.sent_audio_packets;
    std::snprintf(buffer, sizeof(buffer), "听写%u秒 上行%u包 静音%u毫秒",
                  static_cast<unsigned>(elapsed_seconds),
                  static_cast<unsigned>(sent_packets),
                  static_cast<unsigned>(g_runtime.vad.silence_ms));
    return buffer;
  }
  if (g_runtime.session_state == AssistantSessionState::Speaking) {
    const uint32_t elapsed_seconds =
        g_runtime.conversation.speaking_started_at_ms > 0 &&
                now_ms() > g_runtime.conversation.speaking_started_at_ms
            ? static_cast<uint32_t>((now_ms() - g_runtime.conversation.speaking_started_at_ms) / 1000U)
            : 0U;
    const uint32_t incoming_packets =
        g_runtime.incoming_audio_packets >= g_runtime.speaking_incoming_audio_packets_base
            ? (g_runtime.incoming_audio_packets - g_runtime.speaking_incoming_audio_packets_base)
            : g_runtime.incoming_audio_packets;
    std::snprintf(buffer, sizeof(buffer), "播报%u秒 下行%u包",
                  static_cast<unsigned>(elapsed_seconds),
                  static_cast<unsigned>(incoming_packets));
    return buffer;
  }
  if (g_runtime.session_state == AssistantSessionState::Opening ||
      g_runtime.session_state == AssistantSessionState::Closing ||
      (!g_runtime.transport.connected && session_connect_inflight_locked())) {
    const int64_t started = g_runtime.transport.connect_started_at_ms;
    const uint32_t seconds =
        started > 0 && now_ms() > started
            ? static_cast<uint32_t>((now_ms() - started) / 1000)
            : 0U;
    std::snprintf(buffer, sizeof(buffer), "连接%u秒 握手中",
                  static_cast<unsigned>(seconds));
    return buffer;
  }
  if (session_idle_available_locked() && session_active_locked() &&
      session_start_inflight_locked()) {
    return "开始收音中";
  }
  if (session_idle_available_locked()) {
    const int64_t started =
        g_runtime.conversation.idle_entered_at_ms > 0
            ? g_runtime.conversation.idle_entered_at_ms
            : now_ms();
    const uint32_t seconds =
        started > 0 && now_ms() > started
            ? static_cast<uint32_t>((now_ms() - started) / 1000)
            : 0U;
    std::snprintf(buffer, sizeof(buffer), "待机%u秒 已连接",
                  static_cast<unsigned>(seconds));
    return buffer;
  }
  return {};
}

std::string derive_state_label_locked() {
  const ProductSystemState system_state =
      derive_product_system_state(g_runtime);
  switch (system_state) {
  case ProductSystemState::Booting:
    return "启动中";
  case ProductSystemState::Provisioning:
    return "配网中";
  case ProductSystemState::Offline:
    return "等待网络";
  case ProductSystemState::ActivationPending:
    return "待激活";
  case ProductSystemState::Connecting:
  case ProductSystemState::Recovering:
    switch (g_runtime.session_state) {
    case AssistantSessionState::Listening:
      return "正在听";
    case AssistantSessionState::Speaking:
      return "播报中";
    case AssistantSessionState::Opening:
    case AssistantSessionState::Closing:
      return "连接中";
    case AssistantSessionState::Failed:
      return "恢复中";
    case AssistantSessionState::ReadyIdle:
    case AssistantSessionState::Inactive:
    default:
      return "连接中";
    }
  case ProductSystemState::Ready:
    switch (g_runtime.session_state) {
    case AssistantSessionState::Listening:
      return "正在听";
    case AssistantSessionState::Speaking:
      return "播报中";
    case AssistantSessionState::ReadyIdle:
      if (session_start_inflight_locked() && session_active_locked()) {
        return "开始收音";
      }
      return g_runtime.conversation.standby_mode ==
                     StandbyListenMode::ContinuousConversation
                 ? "连续对话"
                 : "待唤醒";
    case AssistantSessionState::Closing:
      return "会话收尾";
    case AssistantSessionState::Opening:
      return "连接中";
    case AssistantSessionState::Failed:
      return "恢复中";
    case AssistantSessionState::Inactive:
    default:
      return session_transport_available_locked() ? "待机" : "设备已就绪";
    }
  default:
    return "待机";
  }
}

std::string derive_state_detail_locked() {
  const ProductSystemState system_state =
      derive_product_system_state(g_runtime);
  switch (system_state) {
  case ProductSystemState::Booting:
    return "系统初始化中";
  case ProductSystemState::Provisioning:
    return "请连接设备热点并打开配网页";
  case ProductSystemState::Offline:
    return "网络未连接";
  case ProductSystemState::ActivationPending:
    return "等待激活完成";
  case ProductSystemState::Connecting:
    if (!g_runtime.transport.connected) {
      return "MQTT transport 建立中";
    }
    if (!session_identity_available_locked()) {
      return "正在准备语音会话";
    }
    if (!session_audio_path_ready_locked()) {
      return "等待 UDP 音频通道";
    }
    return "等待连接完成";
  case ProductSystemState::Ready:
    switch (g_runtime.session_state) {
    case AssistantSessionState::Listening:
      return g_runtime.vad.voice_activity_active ? "检测到说话" : "等待收句";
    case AssistantSessionState::Speaking:
      return g_runtime.diagnostics.last_message_summary.empty()
                 ? "小智正在回复"
                 : g_runtime.diagnostics.last_message_summary;
    case AssistantSessionState::ReadyIdle:
      if (session_start_inflight_locked() && session_active_locked()) {
        return "正在开始收音";
      }
      return g_runtime.conversation.standby_mode ==
                     StandbyListenMode::ContinuousConversation
                 ? "持续监听待命"
                 : "请说唤醒词";
    case AssistantSessionState::Closing:
      return "正在结束本轮会话";
    case AssistantSessionState::Opening:
      return "正在准备语音会话";
    case AssistantSessionState::Failed:
      return "会话恢复中";
    case AssistantSessionState::Inactive:
    default:
      return session_transport_available_locked() ? "后端已连，会话按需"
                                                  : "设备已就绪";
    }
  case ProductSystemState::Recovering:
    return "正在恢复会话";
  default:
    return "设备已就绪";
  }
}

std::string format_runtime_debug_result_locked() {
  char buffer[220] = {};
  const ProductSystemState system_state =
      derive_product_system_state(g_runtime);
  const uint32_t service_age_s =
      g_runtime.transport.last_service_activity_at_ms > 0 &&
              now_ms() > g_runtime.transport.last_service_activity_at_ms
          ? static_cast<uint32_t>((now_ms() - g_runtime.transport.last_service_activity_at_ms) /
                                  1000)
          : 0U;
  if (!g_runtime.diagnostics.last_error.empty()) {
    std::snprintf(buffer, sizeof(buffer), "错误: %s",
                  g_runtime.diagnostics.last_error.c_str());
    return buffer;
  }
  if (!g_runtime.diagnostics.last_transport_error.empty()) {
    std::snprintf(buffer, sizeof(buffer), "传输异常: %s",
                  g_runtime.diagnostics.last_transport_error.c_str());
    return buffer;
  }

  if (system_state == ProductSystemState::Connecting ||
      g_runtime.session_state == AssistantSessionState::Opening ||
      g_runtime.session_state == AssistantSessionState::Closing) {
    if (!assistant_network_connected()) {
      std::snprintf(buffer, sizeof(buffer), "网络未连，等待联网");
      return buffer;
    }
    if (!g_runtime.transport.connected) {
      std::snprintf(buffer, sizeof(buffer), "正在建立服务器连接");
      return buffer;
    }
    if (!session_identity_available_locked()) {
      std::snprintf(buffer, sizeof(buffer), "服务器已连，等待会话握手");
      return buffer;
    }
    if (!session_audio_path_ready_locked()) {
      std::snprintf(buffer, sizeof(buffer), "会话已拿到，等待UDP音频通道");
      return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "连接流程进行中");
    return buffer;
  }

  if (g_runtime.session_state == AssistantSessionState::Listening) {
    std::snprintf(buffer, sizeof(buffer),
                  "传输%s，会话%s，%s，最近停止: %s",
                  g_runtime.transport.connected ? "已连" : "未连",
                  session_ready_locked() ? "已就绪" : "未就绪",
                  g_runtime.vad.voice_activity_active ? "检测到说话" : "等待收句",
                  stop_result_text(g_runtime.conversation.last_stop_result));
    return buffer;
  }

  if (session_idle_available_locked() && session_active_locked() &&
      session_start_inflight_locked()) {
    std::snprintf(buffer, sizeof(buffer), "传输%s，会话%s，开始收音中",
                  g_runtime.transport.connected ? "已连" : "未连",
                  session_ready_locked() ? "已就绪" : "未就绪");
    return buffer;
  }

  if (g_runtime.session_state == AssistantSessionState::Speaking) {
    std::snprintf(buffer, sizeof(buffer),
                  "传输%s，会话%s，正在播报，服务最近%u秒前有响应",
                  g_runtime.transport.connected ? "已连" : "未连",
                  session_ready_locked() ? "已就绪" : "未就绪",
                  static_cast<unsigned>(service_age_s));
    return buffer;
  }

  if (g_runtime.session_state == AssistantSessionState::Failed ||
      system_state == ProductSystemState::Recovering) {
    std::snprintf(buffer, sizeof(buffer), "会话恢复中，等待重连");
    return buffer;
  }

  std::snprintf(buffer, sizeof(buffer),
                "传输%s，会话%s，待机模式%s，最近停止: %s",
                g_runtime.transport.connected ? "已连" : "未连",
                session_ready_locked() ? "已就绪" : "未就绪",
                standby_mode_status_text(g_runtime.conversation.standby_mode),
                stop_result_text(g_runtime.conversation.last_stop_result));
  return buffer;
}

} // namespace lunaredu::pet::conversation::internal
