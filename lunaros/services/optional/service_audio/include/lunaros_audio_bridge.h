#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "lunaros_audio_capture.h"

namespace lunaredu::lunaros {
class KernelRuntime;
class OutputControl;
}

namespace lunaredu::audio {

struct LocalPcmAudioBridgeStatus {
  bool initialized = false;
  bool input_enabled = false;
  bool output_enabled = false;
  uint8_t output_volume_percent = 70;
  uint16_t output_gain_q15 = 16056;
  int input_sample_rate = 0;
  int output_sample_rate = 0;
  uint32_t input_frames = 0;
  uint32_t output_frames = 0;
  uint32_t input_failures = 0;
  uint32_t output_failures = 0;
  uint32_t output_source_peak = 0;
  uint32_t output_effective_peak = 0;
  uint32_t loopback_input_peak = 0;
  uint32_t loopback_input_rms = 0;
  uint32_t loopback_input_clipped_samples = 0;
  uint32_t loopback_input_samples = 0;
  uint32_t reference_frames = 0;
  // Capture-rate carry loss and speaker-rate raw loss have distinct units.
  uint32_t reference_drops = 0;
  uint32_t reference_source_drops = 0;
  uint32_t reference_conversion_failures = 0;
  bool realtime_pipeline = false;
  bool output_muted = true;
  uint32_t submitted_output_samples = 0;
  uint32_t completed_output_samples = 0;
  uint32_t pending_output_samples = 0;
  uint32_t discarded_output_samples = 0;
  uint32_t output_queue_timeouts = 0;
  uint32_t hardware_write_failures = 0;
  uint32_t maximum_hardware_write_us = 0;
  uint32_t maximum_hardware_write_start_gap_us = 0;
  uint64_t maximum_hardware_write_start_gap_at_us = 0;
  float dsp_input_peak = 0.0F;
  float dsp_output_peak = 0.0F;
  float dsp_rms = 0.0F;
  float dsp_limiter_gain = 1.0F;
  float dsp_protection_gain = 1.0F;
  uint64_t dsp_limiter_events = 0;
  uint64_t dsp_soft_clip_samples = 0;
  std::string last_error;
};

struct LocalPcmAudioBridgeDriver {
  void *context = nullptr;
  int input_sample_rate = 0;
  int output_sample_rate = 0;
  bool (*enable_input)(void *context, bool enable) = nullptr;
  bool (*enable_output)(void *context, bool enable) = nullptr;
  bool (*configure_output)(void *context, uint32_t sample_rate) = nullptr;
  bool (*read_input)(void *context, int16_t *samples, size_t sample_count,
                     uint32_t timeout_ms) = nullptr;
  bool (*write_output)(void *context, const int16_t *samples,
                       size_t sample_count, uint16_t gain_q15,
                       uint32_t timeout_ms) = nullptr;
  // Optional true duplex input. The callback must fill mono microphone and
  // reference arrays from one hardware batch and explicitly report its clock.
  bool (*read_duplex)(void *,int16_t *,int16_t *,size_t,uint32_t,PcmDuplexObservation &)=nullptr;
};

// Product-owned driver configuration is accepted only before bridge startup.
bool configure_local_pcm_audio_bridge_driver(const LocalPcmAudioBridgeDriver &driver);
// Audio owns focus binding and bridge retirement together. The product must
// declare/seal a non-preemptible duplex output before starting audio. Native
// backends require the runtime/service task owner; custom drivers may omit it.
bool start_local_pcm_audio_bridge(lunaros::OutputControl &, const char *output,
    lunaros::KernelRuntime *runtime = nullptr, const char *owner_service_id = nullptr);
// Caller must retire its users first. A foreground audio lease blocks teardown;
// false preserves backend ownership for a later bounded retry.
bool stop_local_pcm_audio_bridge(std::string &error);
LocalPcmAudioBridgeStatus local_pcm_audio_bridge_status();
// Hot-path facts only: no strings, loopback RMS, or DSP diagnostic projection.
// Counts retain the same hardware-write acceptance basis as the full status;
// this observation grants no output authority and is not a DMA EOF fence.
struct PcmAudioProgress {
  bool initialized = false, input_enabled = false, output_enabled = false;
  bool realtime_pipeline = false, output_muted = true;
  int input_sample_rate = 0, output_sample_rate = 0;
  uint32_t submitted_output_samples = 0, completed_output_samples = 0;
  uint32_t pending_output_samples = 0, discarded_output_samples = 0;
  uint32_t output_queue_timeouts = 0, hardware_write_failures = 0;
};
PcmAudioProgress local_pcm_audio_bridge_progress();
bool local_pcm_audio_bridge_ready();
// Cheap admission hint for capture owners. Actual reads still check current
// input authority/state; this query does not allocate a diagnostic snapshot.
bool local_pcm_audio_bridge_input_enabled();
bool local_pcm_audio_bridge_enable_input(bool enable);
// Ephemeral turn prewarm; does not enable output or change its generation.
bool local_pcm_audio_bridge_prepare_output();
bool local_pcm_audio_bridge_enable_output(bool enable);
bool local_pcm_audio_bridge_pause_output(bool pause);
bool local_pcm_audio_bridge_set_output_volume(uint8_t percent);
bool local_pcm_audio_bridge_configure_output(uint32_t sample_rate);
bool local_pcm_audio_bridge_read_input(int16_t *samples, size_t sample_count,
                                       uint32_t timeout_ms);
bool local_pcm_audio_bridge_write_output(const int16_t *samples,
                                         size_t sample_count,
                                         uint32_t timeout_ms);
// Capture admission while validating a conversation, then release its state
// lock before waiting for PCM capacity. Stop, focus quiescence, clock changes
// and pause transitions revoke old tickets, including after output reopens.
// This is an epoch check, not an audio-focus authorization grant.
struct PcmOutputTicket { uint64_t generation = 0; };
PcmOutputTicket local_pcm_audio_bridge_output_ticket();
bool local_pcm_audio_bridge_write_output_scoped(PcmOutputTicket ticket,
    const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
bool local_pcm_audio_bridge_read_output_reference(int16_t *samples,
                                                  size_t sample_count);
// One serialized capture owner converts successfully written speaker PCM with
// Audio Effects. Silence/partial reference pads normally; conversion failure
// returns false and stays latched until an explicit clear/lifecycle/rate reset.
struct PcmReferenceObservation {
  uint64_t output_generation=0;
  // Positions in the bridge's resampled reference stream, before padding.
  // A clear/rate reset changes clock_generation. No acoustic alignment implied.
  uint64_t first_sample=0,clock_generation=0;
  uint32_t available_samples=0,padded_samples=0,discarded_samples=0;
  uint32_t age_ms=0; // Since latest reference publication, not a per-sample delay.
  // True only when the backend publishes its hardware-output observer. This
  // is still a software observation, not the speaker's acoustic timestamp.
  bool hardware_observed=false;
  bool source_discontinuous=false,conversion_failed=false;
  uint32_t discarded_source_samples=0; // Speaker-rate samples, not capture-rate samples.
};
bool local_pcm_audio_bridge_read_output_reference(int16_t *,size_t,PcmReferenceObservation *);
struct PcmCaptureObservation {
  PcmCaptureFailure failure=PcmCaptureFailure::None;
  PcmDuplexObservation duplex;
  PcmReferenceObservation reference;
};
// One capture-lane admission covers mic + reference. Hardware duplex is used
// only when explicitly provided; fallback remains software and unaligned.
bool local_pcm_audio_bridge_read_capture(int16_t *,int16_t *,size_t,uint32_t,PcmCaptureObservation *);
void local_pcm_audio_bridge_clear_output_reference();

} // namespace lunaredu::audio
