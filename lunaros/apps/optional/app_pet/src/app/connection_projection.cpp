#include "lunaredu_pet_connections.h"
namespace lunaredu::pet {
bool project_connection_voice(ConnectionsView &out,const VoiceAvailabilityFacts &voice){
  const bool ready=voice.initialized&&voice.enabled&&!voice.suspended&&!out.identity_scope.empty()&&
      (voice.listening||voice.speaking||(voice.activation_ready&&!voice.opening&&!voice.thinking&&voice.microphone_enabled&&voice.network_address));
  return project_connection_voice(out,VoiceControlPresentation{!ready?VoiceAction::None:voice.listening?VoiceAction::FinishInput:voice.speaking?VoiceAction::StopReply:VoiceAction::BeginInput,
      voice.failed,voice.listening,voice.thinking,voice.speaking,{}});
}
bool project_connection_voice(ConnectionsView &out,const VoiceControlPresentation &voice){
  const bool ready=!out.identity_scope.empty()&&voice.action!=VoiceAction::None;
  out.voice.state=ready?ConnectionState::Ready:ConnectionState::Unavailable;
  out.voice.action=ready?voice.action:VoiceAction::None;
  out.voice.action_token=ready?voice.token:std::string();
  out.voice.status=voice.failed?"语音暂不可用":voice.listening?"正在聆听":voice.thinking?"正在思考":voice.speaking?"正在回复":"在这里陪着你";
  return ready;
}
}
