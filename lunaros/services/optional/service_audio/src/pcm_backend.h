#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "lunaros_audio_capture.h"

namespace lunaredu::lunaros {
class KernelRuntime;
}

namespace lunaredu::audio {
struct PcmBackendDiagnostics {
  bool realtime = false;
  bool output_requested = false;
  bool output_muted = true;
  uint32_t submitted_frames = 0;
  uint32_t completed_frames = 0;
  uint32_t pending_frames = 0;
  uint32_t discarded_frames = 0;
  uint32_t queue_timeouts = 0;
  uint32_t output_write_failures = 0;
  uint32_t maximum_write_us = 0;
  uint32_t maximum_write_start_gap_us = 0;
  uint64_t maximum_write_start_gap_at_us = 0;
  float input_peak = 0.0F;
  float output_peak = 0.0F;
  float rms = 0.0F;
  float limiter_gain = 1.0F;
  float protection_gain = 1.0F;
  uint64_t limiter_events = 0;
  uint64_t soft_clip_samples = 0;
};

// Core PCM buffering and volume processing depend only on this backend contract.
class PcmBackend {
public:
  virtual ~PcmBackend() = default;
  virtual int input_sample_rate() const = 0;
  virtual int output_sample_rate() const = 0;
  virtual bool enable_input(bool) = 0;
  // Prepare output capacity ahead of a real turn without enabling hardware or
  // publishing audio. Synchronous backends have no worker to prepare.
  virtual bool prepare_output() { return true; }
  virtual bool enable_output(bool) = 0;
  virtual bool pause_output(bool) { return false; }
  virtual bool configure_output_sample_rate(uint32_t) = 0;
  virtual bool read_input(int16_t *, size_t, uint32_t) = 0;
  virtual bool has_duplex_reference()const{return false;}
  virtual bool read_duplex(int16_t *,int16_t *,size_t,uint32_t,PcmDuplexObservation &){return false;}
  virtual bool write_output(const int16_t *, size_t, uint16_t gain_q15,
                            uint32_t timeout_ms) = 0;
  virtual PcmBackendDiagnostics diagnostics() const { return {}; }
  // Nonblocking retirement. False retains backend/queues/context for retry.
  virtual bool shutdown(std::string &error) { error.clear(); return true; }
  // Called only after both input and output demand have ended, queued output
  // and fade have drained, and I/O has quiesced. Keeps configuration/claims
  // valid for a subsequent enable; does not destroy this backend.
  virtual bool release_idle_resources() { return true; }
  // Synchronous drivers acknowledge here; queued backends override to confirm
  // their existing worker/fade has completed. No wait/task is introduced.
  virtual bool quiesce(std::string &error) {
    const bool input = enable_input(false);
    const bool output = enable_output(false);
    error = input && output ? "" : "audio_device_stop_failed";
    return input && output;
  }
  using OutputObserver = std::function<void(const int16_t *, size_t, uint32_t)>;
  // True means the backend reports successful hardware writes after its DSP.
  virtual bool set_output_observer(OutputObserver) { return false; }
};
std::unique_ptr<PcmBackend> create_pcm_backend(
    lunaredu::lunaros::KernelRuntime *runtime,
    const char *owner_service_id, std::string &error);
} // namespace lunaredu::audio
