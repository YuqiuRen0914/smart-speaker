#pragma once
#include "lunaredu_lunaros/app/audio_focus.h"
#include "lunaredu_audio_bridge.h"
#include <utility>
namespace lunaredu::pet::conversation::audio_io {
#define LUNAROS_RESIDENT_AUDIO(name) \
template <typename... Args> inline bool name(Args &&...args) { \
  lunaredu::lunaros::ResidentAudioAccess access; \
  return access && lunaredu::audio::local_pcm_audio_bridge_##name(std::forward<Args>(args)...); \
}
LUNAROS_RESIDENT_AUDIO(enable_input)
LUNAROS_RESIDENT_AUDIO(enable_output)
LUNAROS_RESIDENT_AUDIO(configure_output)
LUNAROS_RESIDENT_AUDIO(read_input)
LUNAROS_RESIDENT_AUDIO(write_output)
LUNAROS_RESIDENT_AUDIO(read_output_reference)
#undef LUNAROS_RESIDENT_AUDIO
inline void clear_output_reference() {
  lunaredu::lunaros::ResidentAudioAccess access;
  if(access) lunaredu::audio::local_pcm_audio_bridge_clear_output_reference();
}
}
