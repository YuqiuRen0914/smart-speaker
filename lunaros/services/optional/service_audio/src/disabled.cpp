#include "sdkconfig.h"
#include "lunaros_audio_bridge.h"
#if CONFIG_LUNAROS_AUDIO_RUNTIME && !CONFIG_LUNAREDU_DEVICE_ENABLE_AUDIO
#include "pcm_backend.h"
namespace lunaredu::audio {
std::unique_ptr<PcmBackend> create_pcm_backend(
    lunaredu::lunaros::KernelRuntime *, const char *, std::string &error) {
  error = "generic_i2s_audio_backend_disabled";
  return nullptr;
}
}
#endif
#if !CONFIG_LUNAROS_AUDIO_RUNTIME
namespace lunaredu::audio {
bool configure_local_pcm_audio_bridge_driver(const LocalPcmAudioBridgeDriver &) { return false; }
bool start_local_pcm_audio_bridge(lunaredu::lunaros::OutputControl &, const char *, lunaredu::lunaros::KernelRuntime *, const char *) { return false; }
bool stop_local_pcm_audio_bridge(std::string &error) { error.clear(); return true; }
LocalPcmAudioBridgeStatus local_pcm_audio_bridge_status() { LocalPcmAudioBridgeStatus s; s.last_error = "disabled"; return s; }
PcmAudioProgress local_pcm_audio_bridge_progress() { return {}; }
bool local_pcm_audio_bridge_ready() { return false; }
bool local_pcm_audio_bridge_input_enabled() { return false; }
bool local_pcm_audio_bridge_enable_input(bool) { return false; }
bool local_pcm_audio_bridge_prepare_output() { return false; }
bool local_pcm_audio_bridge_enable_output(bool) { return false; }
bool local_pcm_audio_bridge_pause_output(bool) { return false; }
bool local_pcm_audio_bridge_set_output_volume(uint8_t) { return false; }
bool local_pcm_audio_bridge_configure_output(uint32_t) { return false; }
bool local_pcm_audio_bridge_read_input(int16_t *, size_t, uint32_t) { return false; }
bool local_pcm_audio_bridge_read_capture(int16_t *,int16_t *,size_t,uint32_t,PcmCaptureObservation *value){if(value)*value={};return false;}
bool local_pcm_audio_bridge_write_output(const int16_t *, size_t, uint32_t) { return false; }
PcmOutputTicket local_pcm_audio_bridge_output_ticket() { return {}; }
bool local_pcm_audio_bridge_write_output_scoped(PcmOutputTicket, const int16_t *, size_t, uint32_t) { return false; }
bool local_pcm_audio_bridge_read_output_reference(int16_t *, size_t) { return false; }
bool local_pcm_audio_bridge_read_output_reference(int16_t *, size_t,PcmReferenceObservation *value) { if(value)*value={};return false; }
void local_pcm_audio_bridge_clear_output_reference() {}
}
#endif
