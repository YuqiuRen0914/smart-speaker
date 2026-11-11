#pragma once

#include <cstdint>
#include <string>

namespace lunaredu::pet::conversation::internal {

enum class AssistantEventType : uint8_t {
  ConnectRequested = 0,
  ContinuousStartRequested,
  KeepaliveDue,
  StaleSessionCloseDue,
  ConnectionWatchdogDue,
  WakeWordStartRequested,
  AutoStopRequested,
  FlushPendingMcp,
  TransportConnectedWaitSession,
  ConnectFailure,
  TransportDisconnected,
  TransportError,
  SessionHelloReceived,
  AudioChannelReady,
  AudioUplinkPrimed,
  TtsStarted,
  TtsStopped,
  AudioPlaybackFinished,
  SttActivityReceived,
  SessionGoodbye,
  StartListeningRequested,
  ManualStartRequested,
  ConversationControlRequested,
  AbortSpeakingRequested,
  ToggleStandbyMode,
  SetStandbyMode,
  ReopenMqttAudioChannel,
  WakeWordUnavailable,
  LocalAudioInputFailed,
  EmotionReceived,
  ListeningNoSpeechTimeoutDue,
  ContinuousIdleTimeoutDue,
};

constexpr bool assistant_event_survives_suspension(AssistantEventType type) {
  switch (type) {
  case AssistantEventType::WakeWordStartRequested:
  case AssistantEventType::ManualStartRequested:
  case AssistantEventType::ConversationControlRequested:
  case AssistantEventType::AbortSpeakingRequested:
  case AssistantEventType::ToggleStandbyMode:
  case AssistantEventType::SetStandbyMode:
    return true;
  default:
    return false;
  }
}

// Transport loss and standby preference survive a turn cancellation; cloud
// response/start work belongs to the generation in which it was received.
constexpr bool assistant_event_tracks_turn(AssistantEventType type) {
  return type != AssistantEventType::TransportDisconnected &&
      type != AssistantEventType::TransportError &&
      type != AssistantEventType::TransportConnectedWaitSession &&
      type != AssistantEventType::SetStandbyMode &&
      type != AssistantEventType::ToggleStandbyMode;
}

struct AssistantEvent {
  AssistantEventType type = AssistantEventType::ConnectRequested;
  std::string text;
  std::string detail;
  bool flag = false;
  uint32_t generation = 0;
};

} // namespace lunaredu::pet::conversation::internal
