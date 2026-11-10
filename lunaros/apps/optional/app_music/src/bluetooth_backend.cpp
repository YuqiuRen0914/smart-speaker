#include "lunaredu_music_player.h"
#if LUNAROS_MUSIC_BLUETOOTH_COMPONENT
#include "lunaros_bluetooth.h"
#endif
namespace lunaredu::music {
SpeakerSnapshot speaker_snapshot() {
#if LUNAROS_MUSIC_BLUETOOTH_COMPONENT
  const auto s=lunaros::bluetooth_music_snapshot();
  return {s.supported&&s.available,s.enabled,s.receiving,s.connected,s.playing,s.controllable,s.error};
#else
  return {};
#endif
}
bool speaker_action(uint32_t id,const SpeakerSnapshot &s,std::string &error) {
#if LUNAROS_MUSIC_BLUETOOTH_COMPONENT
  using A=lunaros::BluetoothAction;
  if(!s.supported){error="bluetooth_music_unsupported";return false;}
  A action=A::MusicMedia;uint16_t value=0;
  if(id==222) {
    if(!s.enabled&&!s.receiving){error="bluetooth_disabled";return false;}
    action=s.receiving?A::MusicOff:A::MusicOn;
    if(!s.receiving&&!stop_local(error))return false;
  } else {
    if(id==218)value=0xcd;else if(id==219)value=0xb5;else if(id==223)value=0xb6;
    else {error="music_action_unknown";return false;}
    if(!s.enabled||!s.receiving||!s.connected||!s.controllable){error="bluetooth_music_peer_required";return false;}
  }
  uint32_t request=0;return lunaros::request_bluetooth(action,value,"",request,error);
#else
  (void)id;(void)s;error="bluetooth_music_unsupported";return false;
#endif
}
}
