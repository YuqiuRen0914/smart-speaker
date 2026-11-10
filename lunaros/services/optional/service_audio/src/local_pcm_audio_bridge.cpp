#include "lunaros_audio_bridge.h"

#include <sdkconfig.h>
#include "pcm_backend.h"
#include "sample_ring.h"
#include "lunaros_audio_focus.h"
#include "lunaros_audio_resampler.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>



namespace lunaredu::audio {
namespace {

[[maybe_unused]] constexpr const char *kLogTag = "local_pcm_audio";

std::mutex g_lifecycle_mutex;
std::mutex g_mutex;
std::mutex g_output_mutex;
std::mutex g_input_mutex;
// The capture owner serializes SDK conversion; the hardware observer never
// takes this lock or invokes/allocates a converter.
std::mutex g_reference_mutex;
bool g_closing = false;
std::unique_ptr<PcmBackend> g_audio;
LocalPcmAudioBridgeDriver g_driver;
bool g_driver_configured = false;
LocalPcmAudioBridgeStatus g_status;
SampleRing g_output_reference;
SampleRing g_converted_reference;
PcmRateConverter g_reference_converter;
constexpr size_t kReferenceBlockSamples=256;
std::array<int16_t,kReferenceBlockSamples> g_reference_input{};
std::unique_ptr<int16_t[]> g_reference_output;
size_t g_reference_output_capacity=0;
uint64_t g_converter_generation=0;
uint32_t g_converter_source_rate=0,g_converter_target_rate=0;
uint32_t g_reference_source_rate=0;
bool g_reference_discontinuous=false,g_reference_conversion_fault=false;
uint32_t g_reference_pending_drops=0;
bool g_backend_reports_reference = false;
std::chrono::steady_clock::time_point g_reference_written_at;
std::vector<int16_t> g_scaled_output;
uint64_t g_loopback_input_square_sum = 0;
uint64_t g_loopback_input_samples = 0;
// Protected by g_mutex. Output mutation/write ordering is owned by
// g_output_mutex; never reset the epoch when a backend is replaced.
uint64_t g_output_generation = 1;
uint64_t g_reference_clock_generation=1,g_reference_next_sample=0;
uint64_t g_duplex_capture_generation=1,g_duplex_next_sample=0;
void clear_reference_locked() {
  g_output_reference.clear();g_converted_reference.clear();
  ++g_reference_clock_generation;
  g_reference_next_sample=0;
  g_reference_discontinuous=true;g_reference_conversion_fault=false;
  g_reference_written_at={};
}


#ifndef CONFIG_LUNAROS_AUDIO_REFERENCE_SAMPLES
#define CONFIG_LUNAROS_AUDIO_REFERENCE_SAMPLES 8000
#endif
void append_output_reference_locked(const int16_t *samples, size_t count, uint32_t rate) {
  if (!g_status.output_enabled || rate == 0 || g_status.input_sample_rate <= 0) return;
  if(g_reference_source_rate!=rate){clear_reference_locked();g_reference_source_rate=rate;}
  // Retain only successfully written, final speaker PCM. Rate conversion belongs
  // to capture, never to this hardware-write callback.
  const auto dropped=g_output_reference.push(samples,count);
  g_status.reference_source_drops+=dropped;g_reference_pending_drops+=dropped;
  if(dropped) {
    ++g_reference_clock_generation;g_reference_next_sample=0;
    g_converted_reference.clear();g_reference_discontinuous=true;
  }
  ++g_status.reference_frames;
  g_reference_written_at = std::chrono::steady_clock::now();
}

void observe_hardware_output(const int16_t *samples, size_t count, uint32_t rate) {
  std::lock_guard<std::mutex> lock(g_mutex);
  append_output_reference_locked(samples, count, rate);
}

void set_error_locked(const std::string &error) {
  g_status.last_error = error;
  if (!error.empty()) {
    ESP_LOGW(kLogTag, "%s", error.c_str());
  }
}

// Treat the UI value as perceived volume rather than raw sample amplitude.
// A linear amplitude slider makes most of its useful range feel compressed;
// a squared law gives the 0-100 control a practical low-volume range while
// retaining full scale at 100%.
uint16_t volume_gain_q15(uint8_t percent) {
  const uint32_t squared = static_cast<uint32_t>(percent) * percent;
  return static_cast<uint16_t>((squared * 32767U + 5000U) / 10000U);
}

} // namespace

bool configure_local_pcm_audio_bridge_driver(
    const LocalPcmAudioBridgeDriver &driver) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_closing) { set_error_locked("local_audio_stop_pending"); return false; }
  if (g_audio != nullptr || g_status.initialized) {
    set_error_locked("local_audio_already_initialized");
    return false;
  }
  if (driver.input_sample_rate <= 0 || driver.output_sample_rate <= 0 ||
      driver.enable_input == nullptr || driver.enable_output == nullptr ||
      driver.configure_output == nullptr || driver.read_input == nullptr ||
      driver.write_output == nullptr) {
    set_error_locked("invalid_local_audio_driver");
    return false;
  }
  g_driver = driver;
  g_driver_configured = true;
  set_error_locked("");
  return true;
}

namespace {

bool setup_local_pcm_audio_bridge_impl(
    lunaredu::lunaros::KernelRuntime *runtime,
    const char *owner_service_id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_closing) { set_error_locked("local_audio_stop_pending"); return false; }
  if (g_audio != nullptr || (g_driver_configured && g_status.initialized)) {
    return true;
  }
  ++g_output_generation;

  if (!g_output_reference.initialize(CONFIG_LUNAROS_AUDIO_REFERENCE_SAMPLES)) {
    set_error_locked("audio_reference_allocation_failed");
    return false;
  }
  if(!g_converted_reference.initialize(kReferenceBlockSamples)) {
    set_error_locked("audio_reference_carry_allocation_failed");return false;
  }
  if (g_driver_configured) {
    g_status = {};
    g_status.output_volume_percent = 70;
    g_status.output_gain_q15 = volume_gain_q15(70);
    g_status.initialized = true;
    g_status.input_sample_rate = g_driver.input_sample_rate;
    g_status.output_sample_rate = g_driver.output_sample_rate;
    clear_reference_locked();
    set_error_locked("");
    ESP_LOGI(kLogTag, "custom local audio ready input=%d output=%d",
             g_status.input_sample_rate, g_status.output_sample_rate);
    return true;
  }

  std::string error;
  auto audio = create_pcm_backend(runtime, owner_service_id, error);
  if (!audio) { set_error_locked(error); return false; }
  g_status.initialized = true;
  g_status.output_volume_percent = 70;
  g_status.output_gain_q15 = volume_gain_q15(70);
  g_status.input_sample_rate = audio->input_sample_rate();
  g_status.output_sample_rate = audio->output_sample_rate();
  g_backend_reports_reference = audio->set_output_observer(observe_hardware_output);
  g_audio = std::move(audio);
  clear_reference_locked();
  set_error_locked("");
  ESP_LOGI(kLogTag, "I2S local audio ready input=%d output=%d",
           g_status.input_sample_rate, g_status.output_sample_rate);
  return true;
}

} // namespace

namespace {
bool quiesce_device(void *, std::string &error) {
  std::scoped_lock io(g_input_mutex, g_output_mutex);
  // As with bridge retirement, the backend remains borrowed until the owner
  // acknowledges completion. Both lane locks prevent a concurrent driver call.
  PcmBackend *backend;
  LocalPcmAudioBridgeDriver driver;
  bool custom;
  { std::lock_guard<std::mutex> lock(g_mutex);
    // Focus can have been configured before backend allocation failed. No
    // driver/backend has borrowed hardware in this exact empty state.
    if (!g_status.initialized && !g_closing && !g_audio && !g_driver_configured) {
      error.clear();return true;
    }
    if (!g_status.initialized || g_closing) {error="local_audio_not_initialized";return false;}
    ++g_output_generation;
    backend=g_audio.get(); driver=g_driver; custom=g_driver_configured;
  }
  bool stopped;
  if (custom) {
    const bool input=driver.enable_input(driver.context,false);
    const bool output=driver.enable_output(driver.context,false);
    stopped=input && output;
    if (!stopped) error="audio_device_stop_failed";
  } else stopped=backend && backend->quiesce(error);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (stopped) {
    g_status.input_enabled=g_status.output_enabled=false;
    clear_reference_locked(); error.clear();
  }
  return stopped;
}
}
bool start_local_pcm_audio_bridge(lunaros::OutputControl &control, const char *output,
    lunaros::KernelRuntime *runtime, const char *owner_service_id) {
  std::unique_lock<std::mutex> lifecycle(g_lifecycle_mutex, std::try_to_lock);
  if (!lifecycle.owns_lock()) return false;
  std::string error;
  if (!local_audio_focus().configure(control, output, {nullptr, quiesce_device}, error)) {
    std::lock_guard<std::mutex> lock(g_mutex); set_error_locked(error); return false;
  }
  return setup_local_pcm_audio_bridge_impl(runtime, owner_service_id);
}

namespace {
bool stop_backend(void *, std::string &error) {
  std::scoped_lock io(g_input_mutex, g_output_mutex,g_reference_mutex);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_closing = true;
  ++g_output_generation;
  g_status.input_enabled = g_status.output_enabled = false;
  g_status.initialized = false;
  if (g_audio) {
    const bool input_stopped = g_audio->enable_input(false);
    const bool output_stopped = g_audio->enable_output(false);
    if (!input_stopped || !output_stopped) { error = "local_audio_device_stop_failed"; return false; }
    if (!g_audio->shutdown(error)) return false;
    g_audio.reset();
  } else if (g_driver_configured) {
    const bool input_stopped = g_driver.enable_input(g_driver.context, false);
    const bool output_stopped = g_driver.enable_output(g_driver.context, false);
    if (!input_stopped || !output_stopped) {
      error = "local_audio_driver_stop_failed"; return false;
    }
  }
  clear_reference_locked();g_output_reference.release();g_converted_reference.release();
  g_reference_converter.close();g_reference_output.reset();
  g_reference_output_capacity=0;g_converter_generation=0;g_reference_source_rate=0;
  g_converter_source_rate=g_converter_target_rate=0;
  g_reference_pending_drops=0;
  std::vector<int16_t>().swap(g_scaled_output);
  g_status = {}; g_backend_reports_reference = false;
  g_driver = {}; g_driver_configured = false;
  g_loopback_input_square_sum = g_loopback_input_samples = 0;
  g_closing = false; error.clear(); return true;
}
}
bool stop_local_pcm_audio_bridge(std::string &error) {
  std::unique_lock<std::mutex> lifecycle(g_lifecycle_mutex, std::try_to_lock);
  if (!lifecycle.owns_lock()) { error = "local_audio_lifecycle_busy"; return false; }
  return local_audio_focus().stop(stop_backend, nullptr, error);
}

PcmAudioProgress local_pcm_audio_bridge_progress() {
  std::lock_guard<std::mutex> lock(g_mutex);
  PcmAudioProgress status;
  status.initialized=g_status.initialized;
  status.input_enabled=g_status.input_enabled;status.output_enabled=g_status.output_enabled;
  status.input_sample_rate=g_status.input_sample_rate;status.output_sample_rate=g_status.output_sample_rate;
  if(g_audio) {
    const auto backend=g_audio->diagnostics();
    status.realtime_pipeline=backend.realtime;status.output_muted=backend.output_muted;
    status.submitted_output_samples=backend.submitted_frames;
    status.completed_output_samples=backend.completed_frames;
    status.pending_output_samples=backend.pending_frames;
    status.discarded_output_samples=backend.discarded_frames;
    status.output_queue_timeouts=backend.queue_timeouts;
    status.hardware_write_failures=backend.output_write_failures;
  }
  return status;
}

LocalPcmAudioBridgeStatus local_pcm_audio_bridge_status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto status = g_status;
  if (g_audio != nullptr) {
    const auto backend = g_audio->diagnostics();
    status.realtime_pipeline = backend.realtime;
    status.output_muted = backend.output_muted;
    status.submitted_output_samples = backend.submitted_frames;
    status.completed_output_samples = backend.completed_frames;
    status.pending_output_samples = backend.pending_frames;
    status.discarded_output_samples = backend.discarded_frames;
    status.output_queue_timeouts = backend.queue_timeouts;
    status.hardware_write_failures = backend.output_write_failures;
    status.maximum_hardware_write_us = backend.maximum_write_us;
    status.maximum_hardware_write_start_gap_us = backend.maximum_write_start_gap_us;
    status.maximum_hardware_write_start_gap_at_us = backend.maximum_write_start_gap_at_us;
    status.dsp_input_peak = backend.input_peak;
    status.dsp_output_peak = backend.output_peak;
    status.dsp_rms = backend.rms;
    status.dsp_limiter_gain = backend.limiter_gain;
    status.dsp_protection_gain = backend.protection_gain;
    status.dsp_limiter_events = backend.limiter_events;
    status.dsp_soft_clip_samples = backend.soft_clip_samples;
  }
  if (g_loopback_input_samples > 0) {
    status.loopback_input_rms = static_cast<uint32_t>(std::sqrt(
        static_cast<double>(g_loopback_input_square_sum) /
        static_cast<double>(g_loopback_input_samples)));
    status.loopback_input_samples = static_cast<uint32_t>(
        std::min<uint64_t>(g_loopback_input_samples,
                           std::numeric_limits<uint32_t>::max()));
  }
  return status;
}

bool local_pcm_audio_bridge_ready() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return (g_audio != nullptr || g_driver_configured) && g_status.initialized;
}

bool local_pcm_audio_bridge_input_enabled() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return (g_audio != nullptr || g_driver_configured) && g_status.initialized && g_status.input_enabled;
}

bool local_pcm_audio_bridge_enable_input(bool enable) {
  std::lock_guard<std::mutex> input_lock(g_input_mutex);
  PcmBackend *audio = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_closing && enable) { set_error_locked("local_audio_stop_pending"); return false; }
    if (g_audio == nullptr && !g_driver_configured) {
      set_error_locked("local_audio_not_initialized");
      return false;
    }
    audio = g_audio.get();
  }

  const bool ok = g_driver_configured
      ? g_driver.enable_input(g_driver.context, enable)
      : audio->enable_input(enable);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    set_error_locked(enable ? "enable_input_failed" : "disable_input_failed");
    return false;
  }
  if(g_status.input_enabled!=enable){++g_duplex_capture_generation;g_duplex_next_sample=0;}
  g_status.input_enabled = enable;
  set_error_locked("");
  return true;
}

bool local_pcm_audio_bridge_prepare_output() {
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  PcmBackend *audio;
  {std::lock_guard<std::mutex> lock(g_mutex);
    if(g_closing||!g_status.initialized||(!g_audio&&!g_driver_configured))return false;
    if(g_driver_configured)return true;
    audio=g_audio.get();
  }
  return audio->prepare_output();
}

bool local_pcm_audio_bridge_enable_output(bool enable) {
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  PcmBackend *audio = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_closing && enable) { set_error_locked("local_audio_stop_pending"); return false; }
    if (g_audio == nullptr && !g_driver_configured) {
      set_error_locked("local_audio_not_initialized");
      return false;
    }
    audio = g_audio.get();
  }

  const bool ok = g_driver_configured
      ? g_driver.enable_output(g_driver.context, enable)
      : audio->enable_output(enable);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    set_error_locked(enable ? "enable_output_failed" : "disable_output_failed");
    return false;
  }
  if (g_status.output_enabled != enable || !enable) ++g_output_generation;
  g_status.output_enabled = enable;
  if (enable) {
    g_status.output_source_peak = 0;
    g_status.output_effective_peak = 0;
    g_status.loopback_input_peak = 0;
    g_status.loopback_input_rms = 0;
    g_status.loopback_input_clipped_samples = 0;
    g_status.loopback_input_samples = 0;
    g_loopback_input_square_sum = 0;
    g_loopback_input_samples = 0;
  }
  if (!enable) clear_reference_locked();
  set_error_locked("");
  return true;
}

bool local_pcm_audio_bridge_set_output_volume(uint8_t percent) {
  if (percent > 100) {
    return false;
  }
  // Serialize a volume transition with an in-flight PCM write. This makes a
  // control request deterministic: the next accepted buffer uses the new
  // gain, and the status response cannot race the old write path.
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.output_volume_percent = percent;
  g_status.output_gain_q15 = volume_gain_q15(percent);
  return true;
}

bool local_pcm_audio_bridge_configure_output(uint32_t sample_rate) {
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  PcmBackend *audio = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_audio == nullptr && !g_driver_configured) {
      set_error_locked("local_audio_not_initialized");
      return false;
    }
    audio = g_audio.get();
  }

  const bool ok = g_driver_configured
      ? g_driver.configure_output(g_driver.context, sample_rate)
      : audio->configure_output_sample_rate(sample_rate);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    set_error_locked("configure_output_failed");
    return false;
  }
  g_status.output_sample_rate =
      g_driver_configured ? static_cast<int>(sample_rate)
                          : audio->output_sample_rate();
  ++g_output_generation;
  clear_reference_locked();
  set_error_locked("");
  return true;
}

namespace {
bool record_input_result(int16_t *samples,size_t sample_count,bool ok) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    g_status.input_failures += 1;
    set_error_locked("local_audio_read_failed");
    return false;
  }
  if (g_status.output_enabled) {
    uint32_t peak = 0;
    for (size_t index = 0; index < sample_count; ++index) {
      const int32_t sample = samples[index];
      peak = std::max<uint32_t>(
          peak, static_cast<uint32_t>(sample < 0 ? -sample : sample));
      g_loopback_input_square_sum +=
          static_cast<uint64_t>(static_cast<int64_t>(sample) * sample);
      g_loopback_input_samples += 1;
      if (sample == std::numeric_limits<int16_t>::min() ||
          sample == std::numeric_limits<int16_t>::max()) {
        g_status.loopback_input_clipped_samples += 1;
      }
    }
    g_status.loopback_input_peak =
        std::max(g_status.loopback_input_peak, peak);
  }
  g_status.input_frames += 1;
  set_error_locked("");
  return true;
}

}
bool local_pcm_audio_bridge_read_input(int16_t *samples, size_t sample_count,
                                       uint32_t timeout_ms) {
  std::lock_guard<std::mutex> input_lock(g_input_mutex);
  PcmBackend *audio = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if ((g_audio == nullptr && !g_driver_configured) ||
        !g_status.input_enabled) {
      // Disabled input is an intentional resource state while another
      // foreground application owns audio focus, not a hardware read error.
      return false;
    }
    audio = g_audio.get();
  }

  const bool ok = g_driver_configured
      ? g_driver.read_input(g_driver.context, samples, sample_count, timeout_ms)
      : audio->read_input(samples, sample_count, timeout_ms);

  return record_input_result(samples,sample_count,ok);
}

namespace {
bool write_output(PcmOutputTicket ticket, bool scoped, const int16_t *samples,
                  size_t sample_count, uint32_t timeout_ms) {
  if (samples == nullptr || sample_count == 0) {
    return false;
  }
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  PcmBackend *audio = nullptr;
  uint16_t gain_q15 = 32767;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (scoped && (!ticket.generation || ticket.generation != g_output_generation))
      return false; // Cancellation is not a hardware/queue failure.
    if ((g_audio == nullptr && !g_driver_configured) ||
        !g_status.output_enabled) {
      g_status.output_failures += 1;
      set_error_locked("local_audio_output_unavailable");
      return false;
    }
    audio = g_audio.get();
    gain_q15 = g_status.output_gain_q15;
  }

  uint32_t source_peak = 0;
  for (size_t index = 0; index < sample_count; ++index) {
    const int32_t sample = samples[index];
    source_peak = std::max<uint32_t>(
        source_peak, static_cast<uint32_t>(sample < 0 ? -sample : sample));
  }
  const uint32_t effective_peak = static_cast<uint32_t>(
      (static_cast<uint64_t>(source_peak) * gain_q15 + 16383U) / 32767U);
  const int16_t *scaled_samples = samples;
  if (gain_q15 < 32767 && !g_backend_reports_reference) {
    g_scaled_output.resize(sample_count);
    for (size_t index = 0; index < sample_count; ++index) {
      const int64_t scaled =
          (static_cast<int64_t>(samples[index]) * gain_q15 +
           (samples[index] >= 0 ? 16384 : -16384)) /
          32767;
      g_scaled_output[index] = static_cast<int16_t>(std::clamp<int64_t>(
          scaled, std::numeric_limits<int16_t>::min(),
          std::numeric_limits<int16_t>::max()));
    }
    scaled_samples = g_scaled_output.data();
  }

  const bool ok = g_driver_configured
      ? g_driver.write_output(g_driver.context, samples, sample_count,
                              gain_q15, timeout_ms)
      : audio->write_output(samples, sample_count, gain_q15, timeout_ms);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    g_status.output_failures += 1;
    set_error_locked("local_audio_write_failed");
    return false;
  }
  g_status.output_source_peak =
      std::max(g_status.output_source_peak, source_peak);
  g_status.output_effective_peak =
      std::max(g_status.output_effective_peak, effective_peak);
  if (!g_backend_reports_reference) append_output_reference_locked(scaled_samples, sample_count, g_status.output_sample_rate);
  g_status.output_frames += 1;
  set_error_locked("");
  return true;
}
}
bool local_pcm_audio_bridge_write_output(const int16_t *samples,
    size_t sample_count, uint32_t timeout_ms) {
  return write_output({},false,samples,sample_count,timeout_ms);
}
PcmOutputTicket local_pcm_audio_bridge_output_ticket() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return {g_status.initialized && g_status.output_enabled && !g_closing
      ? g_output_generation : 0};
}
bool local_pcm_audio_bridge_write_output_scoped(PcmOutputTicket ticket,
    const int16_t *samples, size_t sample_count, uint32_t timeout_ms) {
  return write_output(ticket,true,samples,sample_count,timeout_ms);
}

bool local_pcm_audio_bridge_pause_output(bool pause) {
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_output_generation;
  return g_audio != nullptr && g_audio->pause_output(pause);
}

bool local_pcm_audio_bridge_read_output_reference(int16_t *samples,
                                                  size_t sample_count) {
  return local_pcm_audio_bridge_read_output_reference(samples,sample_count,nullptr);
}
bool local_pcm_audio_bridge_read_output_reference(int16_t *samples,size_t sample_count,
    PcmReferenceObservation *observation) {
  if(!samples||!sample_count)return false;
  std::fill_n(samples,sample_count,0);
  PcmReferenceObservation result;
  std::lock_guard<std::mutex> owner(g_reference_mutex);
  size_t copied=0;
  uint64_t epoch=0;
  uint32_t source_rate=0,target_rate=0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    result.output_generation=g_output_generation;
    result.hardware_observed=g_backend_reports_reference;
    if(g_reference_written_at!=std::chrono::steady_clock::time_point{}) {
      result.age_ms=uint32_t(std::clamp<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-g_reference_written_at).count(),0,UINT32_MAX));
      if(result.age_ms>120)clear_reference_locked();
    }
    source_rate=g_reference_source_rate;
    target_rate=uint32_t(std::max(0,g_status.input_sample_rate));
    // One requested block plus 20 ms, expressed in speaker samples. A trim
    // invalidates SDK history rather than joining across a source-clock gap.
    const uint64_t maximum=target_rate?
      ((uint64_t(sample_count)+target_rate/50)*source_rate+target_rate-1)/target_rate:0;
    const auto trimmed=g_output_reference.discard(g_output_reference.size()>maximum?
      g_output_reference.size()-size_t(maximum):0);
    g_status.reference_source_drops+=trimmed;g_reference_pending_drops+=trimmed;
    if(trimmed) {
      ++g_reference_clock_generation;g_reference_next_sample=0;
      g_converted_reference.clear();g_reference_discontinuous=true;
    }
    epoch=g_reference_clock_generation;result.clock_generation=epoch;
    result.source_discontinuous=g_reference_discontinuous;
    result.discarded_source_samples=g_reference_pending_drops;
    g_reference_pending_drops=0;g_reference_discontinuous=false;
  }
  // Bounded source blocks, no new task or full PCM window. Carry retains only
  // the small conversion surplus between capture reads.
  size_t budget=CONFIG_LUNAROS_AUDIO_REFERENCE_SAMPLES;
  bool failed=false,changed=false;
  while(copied<sample_count) {
    size_t count=0;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if(epoch!=g_reference_clock_generation){changed=true;break;}
      if(g_reference_conversion_fault){failed=true;break;}
      if(g_converted_reference.size()) {
        if(!copied)result.first_sample=g_reference_next_sample-g_converted_reference.size();
        const auto take=std::min(sample_count-copied,g_converted_reference.size());
        copied+=g_converted_reference.pop(samples+copied,take);
      }
      if(copied==sample_count||!budget||!g_output_reference.size())break;
      if(!source_rate||!target_rate){failed=true;break;}
      if(source_rate==target_rate) {
        // Identity conversion has no filter history or surplus. Copy directly
        // from the observed speaker ring while retaining the clock/epoch fence.
        // No converter scratch is needed for the common 16 kHz duplex route.
        if(!copied)result.first_sample=g_reference_next_sample;
        const auto take=std::min({sample_count-copied,g_output_reference.size(),budget});
        copied+=g_output_reference.pop(samples+copied,take);
        g_reference_next_sample+=take;budget-=take;
        continue;
      }
      const auto wanted=(uint64_t(sample_count-copied)*source_rate+target_rate-1)/target_rate;
      count=std::min({kReferenceBlockSamples,g_output_reference.size(),budget,size_t(std::max<uint64_t>(1,wanted))});
      g_output_reference.pop(g_reference_input.data(),count);
      budget-=count;
    }
    if(g_converter_generation!=epoch) {
      const bool same_rate=source_rate==g_converter_source_rate&&target_rate==g_converter_target_rate;
      if(!(same_rate?g_reference_converter.reset():g_reference_converter.configure(source_rate,target_rate))) {
        failed=true;break;
      }
      g_converter_source_rate=source_rate;g_converter_target_rate=target_rate;
      const auto capacity=g_reference_converter.maximum_output_samples(kReferenceBlockSamples);
      if(!capacity){failed=true;break;}
      if(capacity>g_reference_output_capacity) {
        std::unique_ptr<int16_t[]> next(new(std::nothrow)int16_t[capacity]);
        if(!next){failed=true;break;}
        g_reference_output=std::move(next);g_reference_output_capacity=capacity;
      }
      g_converter_generation=epoch;
    }
    size_t written=0;
    if(!g_reference_converter.process(g_reference_input.data(),count,g_reference_output.get(),
        g_reference_output_capacity,written)){failed=true;break;}
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if(epoch!=g_reference_clock_generation){changed=true;break;}
      if(!copied&&written)result.first_sample=g_reference_next_sample;
      const auto take=std::min(sample_count-copied,written);
      std::copy_n(g_reference_output.get(),take,samples+copied);copied+=take;
      for(size_t i=take;i<written;++i) {
        if(g_converted_reference.push(g_reference_output[i])) {
          ++g_status.reference_drops;++result.discarded_samples;result.source_discontinuous=true;
        }
      }
      g_reference_next_sample+=written;
    }
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    changed=changed||epoch!=g_reference_clock_generation;
    if(changed) {
      // Never deliver a block composed from two output generations.
      copied=0;result.source_discontinuous=true;
      result.output_generation=g_output_generation;
      result.clock_generation=g_reference_clock_generation;result.first_sample=0;
      failed=false; // An obsolete converter failure cannot fail the new owner.
    } else if(failed&&!g_reference_conversion_fault) {
      g_reference_conversion_fault=true;++g_status.reference_conversion_failures;
      set_error_locked("audio_reference_conversion_failed");
    }
  }
  if(failed||changed){std::fill_n(samples,sample_count,0);copied=0;}
  result.conversion_failed=failed;
  result.available_samples=uint32_t(copied);result.padded_samples=uint32_t(sample_count-copied);
  if(observation)*observation=result;
  return !failed;
}

void local_pcm_audio_bridge_clear_output_reference() {
  std::lock_guard<std::mutex> lock(g_mutex);
  clear_reference_locked();
}

bool local_pcm_audio_bridge_read_capture(int16_t *microphone,int16_t *reference,size_t count,
    uint32_t timeout_ms,PcmCaptureObservation *observation) {
  PcmCaptureObservation result;
  if(observation)*observation={};
  if(!microphone||!reference||!count||count>SIZE_MAX/sizeof(int16_t))return false;
  const auto mic_address=reinterpret_cast<uintptr_t>(microphone),ref_address=reinterpret_cast<uintptr_t>(reference);
  if((mic_address>=ref_address?mic_address-ref_address:ref_address-mic_address)<count*sizeof(int16_t))return false;
  std::fill_n(reference,count,0);
  std::unique_lock<std::mutex> input(g_input_mutex);
  PcmBackend *backend;
  LocalPcmAudioBridgeDriver driver;
  bool custom,duplex;
  uint32_t input_rate;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if(g_closing||!g_status.initialized||!g_status.input_enabled)return false;
    backend=g_audio.get();driver=g_driver;custom=g_driver_configured;
    duplex=custom?driver.read_duplex!=nullptr:backend&&backend->has_duplex_reference();
    input_rate=uint32_t(g_status.input_sample_rate);
  }
  if(!duplex) {
    input.unlock();
    if(!local_pcm_audio_bridge_read_input(microphone,count,timeout_ms)){
      result.failure=PcmCaptureFailure::InputRead;if(observation)*observation=result;return false;
    }
    const bool ok=local_pcm_audio_bridge_read_output_reference(reference,count,&result.reference);
    if(!ok)result.failure=PcmCaptureFailure::ReferenceConversion;
    result.duplex.reference_source=PcmReferenceSource::SoftwareOutput;
    result.duplex.sample_rate=input_rate;result.duplex.reference_samples=result.reference.available_samples;
    if(observation)*observation=result;
    return ok;
  }
  auto &metadata=result.duplex;
  const bool read=custom?driver.read_duplex(driver.context,microphone,reference,count,timeout_ms,metadata):
    backend->read_duplex(microphone,reference,count,timeout_ms,metadata);
  const bool valid=read&&metadata.synchronous_reference&&metadata.sample_rate==input_rate&&
    metadata.reference_samples>0&&metadata.reference_samples<=count&&(!metadata.sample_clock_valid||metadata.capture_generation!=0)&&
    (!metadata.monotonic_time_valid||metadata.captured_at_us>=0);
  if(!record_input_result(microphone,count,valid)) {
    result.failure=read?PcmCaptureFailure::InvalidDuplex:PcmCaptureFailure::InputRead;
    if(observation)*observation=result;
    std::fill_n(reference,count,0);return false; // Advertised duplex never silently falls back.
  }
  metadata.reference_source=PcmReferenceSource::HardwareInput;
  std::fill(reference+metadata.reference_samples,reference+count,0);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if(g_closing||!g_status.input_enabled)return false;
    result.reference.output_generation=g_output_generation;
    result.reference.clock_generation=metadata.sample_clock_valid?metadata.capture_generation:g_duplex_capture_generation;
    result.reference.first_sample=metadata.sample_clock_valid?metadata.first_sample:g_duplex_next_sample;
    result.reference.available_samples=uint32_t(metadata.reference_samples);
    result.reference.padded_samples=uint32_t(count-metadata.reference_samples);
    g_duplex_next_sample+=count;
  }
  if(observation)*observation=result;
  return true;
}

} // namespace lunaredu::audio
