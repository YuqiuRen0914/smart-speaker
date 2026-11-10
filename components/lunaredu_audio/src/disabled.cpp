#include "sdkconfig.h"
#include "lunaredu_audio_bridge.h"
#if !CONFIG_LUNAREDU_DEVICE_ENABLE_AUDIO
namespace lunaredu::audio {
bool configure_local_pcm_audio_bridge_driver(const LocalPcmAudioBridgeDriver &) { return false; }
bool setup_local_pcm_audio_bridge() { return false; }
LocalPcmAudioBridgeStatus local_pcm_audio_bridge_status() { LocalPcmAudioBridgeStatus s; s.last_error = "disabled"; return s; }
bool local_pcm_audio_bridge_ready() { return false; }
bool local_pcm_audio_bridge_enable_input(bool) { return false; }
bool local_pcm_audio_bridge_enable_output(bool) { return false; }
bool local_pcm_audio_bridge_set_output_volume(uint8_t) { return false; }
bool local_pcm_audio_bridge_configure_output(uint32_t) { return false; }
bool local_pcm_audio_bridge_read_input(int16_t *, size_t, uint32_t) { return false; }
bool local_pcm_audio_bridge_write_output(const int16_t *, size_t, uint32_t) { return false; }
bool local_pcm_audio_bridge_read_output_reference(int16_t *, size_t) { return false; }
void local_pcm_audio_bridge_clear_output_reference() {}
}
#endif
