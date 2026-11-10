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
  case AssistantEventType::AbortSpeakingRequested:
  case AssistantEventType::ToggleStandbyMode:
  case AssistantEventType::SetStandbyMode:
    return true;
  default:
    return false;
  }
}

struct AssistantEvent {
  AssistantEventType type = AssistantEventType::ConnectRequested;
  std::string text;
  std::string detail;
  bool flag = false;
};

} // namespace lunaredu::pet::conversation::internal
