#include "realtime_pcm_backend.h"
#include "output_cadence.h"
#include "gmf_pcm_pipeline.h"
#include "realtime_dsp_storage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <new>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifndef CONFIG_LUNAROS_AUDIO_REALTIME_OUTPUT_CORE
#define CONFIG_LUNAROS_AUDIO_REALTIME_OUTPUT_CORE 0
#endif

#include "kernel/managed_task.h"
#include "kernel/task_signal.h"
#include "kernel/runtime.h"
#include "lunaros_audio_dsp.h"
#include "diagnostics/runtime_work_profile.h"
#include "diagnostics/cpu_monitor.h"

namespace lunaredu::audio {
namespace {

constexpr const char *kTag = "realtime_pcm";
constexpr size_t kFramesPerBlock = 128;
constexpr size_t kBlockCount = 16;
static_assert(kFramesPerBlock == GmfPcmPipeline::kMaximumFrames);
constexpr uint32_t kFadeMs = 40;
constexpr TickType_t kQueuePoll = pdMS_TO_TICKS(2);
constexpr uint32_t kHardwareWriteTimeoutMs = 12;
constexpr uint32_t kTaskStackBytes = 8192;
constexpr UBaseType_t kTaskPriority = 18;
constexpr int kOutputCore = CONFIG_LUNAROS_AUDIO_REALTIME_OUTPUT_CORE;
static_assert(kOutputCore >= 0 && kOutputCore <= 1);
constexpr uint64_t kIdleGraceUs = 5000000;

struct AudioBlock {
  uint16_t frames = 0;
  uint16_t gain_q15 = 32767;
  uint32_t generation = 0;
  uint64_t queued_us = 0;
  std::array<int16_t, kFramesPerBlock> samples{};
};

class RealtimePcmBackend final : public PcmBackend {
public:
  explicit RealtimePcmBackend(std::unique_ptr<PcmBackend> hardware)
      : hardware_(std::move(hardware)) {}

  ~RealtimePcmBackend() override {
    // Destruction cannot report Pending; fail closed until no worker can touch
    // this object. Normal owners call shutdown() before releasing ownership.
    std::string error;
    while (!shutdown(error)) vTaskDelay(1);
  }

  bool initialize(lunaredu::lunaros::KernelRuntime &runtime,
                  const char *owner_service_id, std::string &error) {
    runtime_ = &runtime;
    if (hardware_ == nullptr || hardware_->input_sample_rate() <= 0 ||
        hardware_->output_sample_rate() <= 0) {
      error = "invalid_realtime_pcm_hardware";
      return false;
    }
    owner_service_id_ = owner_service_id;
    processor_ = internal::make_realtime_dsp();
    if (!processor_) { error = "realtime_pcm_dsp_allocation_failed";return false; }
    if (!processor_->configure(static_cast<uint32_t>(hardware_->output_sample_rate()),
            dsp::preset_settings(dsp::Preset::kFlat))) {
      error = "realtime_pcm_dsp_configuration_failed";return false;
    }
    processor_->set_volume(dsp::kSystemMaxVolume);
    error.clear();return true;
  }

  // producer_mutex_ serializes demand publication and native handle reuse.
  // Reuse the tiny queue controls after idle retirement; no producer can be
  // holding a queue that a warm restart destroys. Large worker stack and the
  // hardware DMA channels are not retained.
  bool ensure_output_worker_locked(std::string &error) {
    if (closing_) { error="realtime_pcm_closing";return false; }
    if (task_ && !exited_.load(std::memory_order_acquire))return true;
    if (task_) {
      const uint64_t start=esp_timer_get_time();
      while(runtime_->task_active("audio.realtime_pcm")) {
        if(uint64_t(esp_timer_get_time())-start>=20000) {
          error="realtime_pcm_retirement_pending";return false;
        }
        vTaskDelay(1);
      }
      task_=nullptr;
    }
    if (!free_queue_)free_queue_ = xQueueCreate(kBlockCount, sizeof(uint8_t));
    if (!ready_queue_)ready_queue_ = xQueueCreate(kBlockCount, sizeof(uint8_t));
    if (free_queue_ == nullptr || ready_queue_ == nullptr) {
      error = "realtime_pcm_queue_allocation_failed";
      if(free_queue_)vQueueDelete(free_queue_);
      if(ready_queue_)vQueueDelete(ready_queue_);
      free_queue_=ready_queue_=nullptr;
      return false;
    }
    if (!queues_seeded_)for (uint8_t index = 0; index < kBlockCount; ++index) {
      (void)xQueueSend(free_queue_, &index, 0);
    }
    queues_seeded_=true;
    // The bridge supplies the perceptual Q15 gain per block. Keep the shared
    // DSP itself at unity so volume is applied once, before I2S expansion.
    stop_.store(false, std::memory_order_release);
    exited_.store(false, std::memory_order_release);
    const lunaredu::lunaros::ManagedTaskDefinition task_definition{
        .id = "audio.realtime_pcm",
        .owner_service_id = owner_service_id_.c_str(),
        .role = "audio_output",
        .stack_bytes = kTaskStackBytes,
        .priority = kTaskPriority,
        .core_affinity = kOutputCore,
        .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Feature,
        .memory = lunaredu::lunaros::ManagedTaskMemory::Internal,
    };
    if (!lunaredu::lunaros::create_managed_task(
            *runtime_, task_definition, task_entry, this, &task_, error)) {
      exited_.store(true, std::memory_order_release);
      if (error.empty()) error = "realtime_pcm_task_creation_failed";
      vQueueDelete(free_queue_);vQueueDelete(ready_queue_);
      free_queue_=ready_queue_=nullptr;queues_seeded_=false;
      return false;
    }
    error.clear();
    ESP_LOGI(kTag, "ready input=%d output=%d blocks=%u frames=%u",
             input_sample_rate(), output_sample_rate(),
             static_cast<unsigned>(kBlockCount),
             static_cast<unsigned>(kFramesPerBlock));
    return true;
  }

  int input_sample_rate() const override {
    return hardware_->input_sample_rate();
  }

  int output_sample_rate() const override {
    return hardware_->output_sample_rate();
  }

  bool enable_input(bool enabled) override {
    std::lock_guard<std::mutex> producer_lock(producer_mutex_);
    if(closing_&&enabled)return false;
    if(!hardware_->enable_input(enabled))return false;
    input_requested_.store(enabled,std::memory_order_release);
    if(!enabled&&(!task_||exited_.load(std::memory_order_acquire))&&
       !output_requested_.load(std::memory_order_acquire)&&!hardware_output_enabled_.load(std::memory_order_acquire))
      return hardware_->release_idle_resources();
    wake_locked();return true;
  }

  bool prepare_output() override {
    std::lock_guard<std::mutex> producer_lock(producer_mutex_);
    std::string error;
    if(!ensure_output_worker_locked(error))return false;
    // A turn prewarm restarts the existing idle grace, not permanent mic demand.
    prepared_at_us_=uint64_t(esp_timer_get_time());
    wake_locked();return true;
  }

  bool enable_output(bool enabled) override {
    std::lock_guard<std::mutex> producer_lock(producer_mutex_);
    if (enabled) {
      std::string error;
      if(!ensure_output_worker_locked(error))return false;
      if (!hardware_output_enabled_.load(std::memory_order_acquire) &&
          !hardware_->enable_output(true)) {
        return false;
      }
      hardware_output_enabled_.store(true, std::memory_order_release);
    } else {
      output_requested_.store(false, std::memory_order_release);
      paused_.store(false, std::memory_order_release);
      drain_ready_queue();
    }
    output_requested_.store(enabled, std::memory_order_release);
    mute_target_.store(!enabled, std::memory_order_release);
    control_generation_.fetch_add(1, std::memory_order_acq_rel);
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.output_requested = enabled;
    }
    wake_locked();
    return true;
  }

  bool pause_output(bool pause) override {
    std::lock_guard<std::mutex> producer_lock(producer_mutex_);
    if(paused_.exchange(pause,std::memory_order_acq_rel)!=pause)
      write_pause_revision_.fetch_add(1,std::memory_order_acq_rel);
    wake_locked(); return true;
  }

  bool configure_output_sample_rate(uint32_t sample_rate) override {
    std::lock_guard<std::mutex> producer_lock(producer_mutex_);
    if (closing_) return false;
    if (sample_rate == static_cast<uint32_t>(output_sample_rate())) return true;
    if (output_requested_.load(std::memory_order_acquire)) {
      return false;
    }
    // A stopped producer may still have an I2S write/fade in flight. Retain the
    // current format until quiescence; GMF and hardware must change together.
    if (hardware_output_enabled_.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> processing_lock(processing_mutex_);
    if (!pipeline_.close()) return false;
    if (!hardware_->configure_output_sample_rate(sample_rate)) return false;
    return processor_->configure(
        sample_rate, dsp::preset_settings(dsp::Preset::kFlat));
  }

  bool read_input(int16_t *samples, size_t sample_count,
                  uint32_t timeout_ms) override {
    return hardware_->read_input(samples, sample_count, timeout_ms);
  }
  bool has_duplex_reference()const override{return hardware_->has_duplex_reference();}
  bool read_duplex(int16_t *mic,int16_t *ref,size_t count,uint32_t timeout,PcmDuplexObservation &out)override {
    return hardware_->read_duplex(mic,ref,count,timeout,out);
  }

  bool write_output(const int16_t *samples, size_t sample_count,
                    uint16_t gain_q15, uint32_t timeout_ms) override {
    if (samples == nullptr || sample_count == 0 || free_queue_ == nullptr ||
        ready_queue_ == nullptr ||
        !output_requested_.load(std::memory_order_acquire)) {
      return false;
    }
    size_t offset = 0;
    const uint32_t generation = control_generation_.load(std::memory_order_acquire);
    const int64_t deadline=esp_timer_get_time()+int64_t(timeout_ms)*1000;
    while (offset < sample_count) {
      uint8_t index = 0;
      for(;;) {
        if(!output_requested_.load(std::memory_order_acquire)||
            generation!=control_generation_.load(std::memory_order_acquire))return false;
        const int64_t remaining=deadline-esp_timer_get_time();
        const uint32_t slice=remaining>0?uint32_t(std::min<int64_t>(12,(remaining+999)/1000)):0;
        const TickType_t wait=slice?std::max<TickType_t>(1,pdMS_TO_TICKS(slice)):0;
        if(xQueueReceive(free_queue_,&index,wait)==pdTRUE)break;
        if(!output_requested_.load(std::memory_order_acquire)||
            generation!=control_generation_.load(std::memory_order_acquire))return false;
        // A 12 ms cancellation probe is not the caller's whole-frame timeout.
        // Share one deadline across all PCM blocks; never restart the budget
        // after a probe or a partially queued frame.
        if(esp_timer_get_time()>=deadline) {
          std::lock_guard<std::mutex> lock(status_mutex_);
          ++status_.queue_timeouts;return false;
        }
      }
      std::lock_guard<std::mutex> producer_lock(producer_mutex_);
      if (!output_requested_.load(std::memory_order_acquire) ||
          generation != control_generation_.load(std::memory_order_acquire)) {
        (void)xQueueSend(free_queue_, &index, 0);
        return false;
      }
      AudioBlock &block = blocks_[index];
      block.generation = generation;
      block.frames = static_cast<uint16_t>(
          std::min(kFramesPerBlock, sample_count - offset));
      block.gain_q15 = gain_q15;
      std::copy_n(samples + offset, block.frames, block.samples.begin());
      {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.submitted_frames += block.frames;
        status_.pending_frames += block.frames;
      }
      block.queued_us = lunaredu::system::worker_work_profiling_active() ? esp_timer_get_time() : 0;
      if (xQueueSend(ready_queue_, &index, 0) != pdTRUE) {
        (void)xQueueSend(free_queue_, &index, 0);
        std::lock_guard<std::mutex> lock(status_mutex_);
        ++status_.queue_timeouts;
        status_.pending_frames -= block.frames;
        status_.discarded_frames += block.frames;
        return false;
      }
      wake_locked();
      offset += block.frames;
    }
    return true;
  }

  PcmBackendDiagnostics diagnostics() const override {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
  }

  bool set_output_observer(OutputObserver observer) override {
    std::lock_guard<std::mutex> lock(status_mutex_);
    output_observer_ = std::move(observer);
    return true;
  }

private:
  // Publication of exit and notification share the producer lock. A caller
  // cannot signal the managed handle after its worker returned to the wrapper.
  void wake_locked() {
    if(task_ && !exited_.load(std::memory_order_acquire))
      lunaredu::lunaros::task_signal::notify(task_);
  }
  static void task_entry(void *context) {
    static_cast<RealtimePcmBackend *>(context)->run();
  }

  void run() {
    uint32_t seen_generation = 0;
    uint64_t idle_since=0;
    uint64_t fade_deadline_us=0;
    while (!stop_.load(std::memory_order_acquire)) {
      if (paused_.load(std::memory_order_acquire)) {
        lunaredu::lunaros::task_signal::wait_ms(lunaredu::lunaros::task_signal::kWaitForever); continue;
      }
      const uint32_t generation =
          control_generation_.load(std::memory_order_acquire);
      if (generation != seen_generation) {
        std::lock_guard<std::mutex> processing_lock(processing_mutex_);
        seen_generation = generation;
        if (mute_target_.load(std::memory_order_acquire)) {
          processor_->request_mute(kFadeMs);
        } else {
          processor_->request_unmute(kFadeMs);
        }
      }

      uint8_t index = 0;
      if (xQueueReceive(ready_queue_, &index, 0) == pdTRUE) {
        AudioBlock &block = blocks_[index];
        if (lunaredu::system::worker_work_profiling_active()) {
          const auto now=uint64_t(esp_timer_get_time());
          if (block.queued_us) maximum_queue_age_us_=std::max(maximum_queue_age_us_,now-block.queued_us);
          if (empty_since_us_) maximum_empty_wait_us_=std::max(maximum_empty_wait_us_,now-empty_since_us_);
        }
        empty_since_us_=0;
        if (output_requested_.load(std::memory_order_acquire) &&
            block.generation == control_generation_.load(std::memory_order_acquire)) {
          process_block(block.samples.data(), block.frames, block.gain_q15, block.generation);
        } else {
          std::lock_guard<std::mutex> lock(status_mutex_);
          status_.pending_frames -= block.frames;
          status_.discarded_frames += block.frames;
        }
        (void)xQueueSend(free_queue_, &index, 0);
        continue;
      }

      if (lunaredu::system::worker_work_profiling_active()) {
        if (!empty_since_us_) empty_since_us_=esp_timer_get_time();
      } else empty_since_us_=maximum_queue_age_us_=maximum_empty_wait_us_=0;
      bool fading = false;
      {
        std::lock_guard<std::mutex> processing_lock(processing_mutex_);
        fading = processor_->fade_state() == dsp::FadeState::kRampDown;
      }
      if (fading) {
        // Keep the existing fade cadence; data/control arriving during this
        // wait is rechecked immediately, but cannot keep restarting its clock.
        // Repeated quiesce notifications must still allow the fade to finish.
        const auto now=uint64_t(esp_timer_get_time());
        if(!fade_deadline_us)fade_deadline_us=now+2000;
        if(now<fade_deadline_us){
          (void)lunaredu::lunaros::task_signal::wait_ms(uint32_t((fade_deadline_us-now+999)/1000));
          continue;
        }
        fade_deadline_us=now+2000;
        process_block(nullptr, kFramesPerBlock, 32767, generation);
        continue;
      }
      fade_deadline_us=0;
      {
        std::lock_guard<std::mutex> producer_lock(producer_mutex_);
        std::lock_guard<std::mutex> processing_lock(processing_mutex_);
        if (processor_->muted() &&
            !output_requested_.load(std::memory_order_acquire) &&
            hardware_output_enabled_.load(std::memory_order_acquire)) {
          if (hardware_->enable_output(false))
            hardware_output_enabled_.store(false, std::memory_order_release);
        }
        if(!output_requested_.load(std::memory_order_acquire) && processor_->muted()) {
          const auto now=static_cast<uint64_t>(esp_timer_get_time());
          if(!idle_since)idle_since=now;
          idle_since=std::max(idle_since,prepared_at_us_);
          if(now-idle_since>=kIdleGraceUs) {
            // Capture can keep duplex clocks/DMA; only the output worker and
            // its pipeline retire while a microphone is live.
            if(!hardware_output_enabled_.load(std::memory_order_acquire) &&
               pipeline_.close() && (input_requested_.load(std::memory_order_acquire)||hardware_->release_idle_resources())) {
              exited_.store(true,std::memory_order_release);return;
            }
            idle_since=now; // retain ownership and retry after another grace
          }
        } else idle_since=0;
      }
      // The producer signals after queue publication, so a wake before this
      // wait is retained. Empty/paused output has no periodic deadline.
      const uint32_t wait=idle_since
          ? static_cast<uint32_t>(std::max<uint64_t>(1,(kIdleGraceUs-
              std::min(kIdleGraceUs,uint64_t(esp_timer_get_time())-idle_since)+999)/1000))
          : lunaredu::lunaros::task_signal::kWaitForever;
      lunaredu::lunaros::task_signal::wait_ms(wait);
    }
    std::lock_guard<std::mutex> producer_lock(producer_mutex_);
    exited_.store(true, std::memory_order_release);
  }

  static bool transform_callback(void *context, const int16_t *samples,
                                 int16_t *output, size_t frames) {
    auto &self = *static_cast<RealtimePcmBackend *>(context);
    self.transform_ran_ = true;
    self.transform_samples(samples, output, frames);
    self.output_profile_.mark(0);
    return true;
  }

  static GmfPcmPipeline::Result writer_callback(void *context,
                                                const int16_t *samples, size_t frames) {
    auto &self = *static_cast<RealtimePcmBackend *>(context);
    if (self.block_generation_ != self.control_generation_.load(std::memory_order_acquire) ||
        (!self.silence_block_ && !self.output_requested_.load(std::memory_order_acquire)))
      return GmfPcmPipeline::Result::Cancelled;
    const auto started = esp_timer_get_time();
    self.block_write_started_us_ = uint64_t(started);
    self.block_write_generation_ = (uint64_t(self.control_generation_.load(std::memory_order_acquire)) << 32) |
        self.write_pause_revision_.load(std::memory_order_acquire);
    self.block_write_gap_ = self.write_cadence_.observe(!self.silence_block_, self.block_write_generation_,
        [started] { return uint64_t(started); });
    const bool ok = self.hardware_->write_output(samples, frames, 32767, kHardwareWriteTimeoutMs);
    self.block_write_us_ = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - started));
    // The reference survives the synchronous vendor call and is published only
    // after the successful device write, outside all pipeline/status locks.
    if (ok) std::copy_n(samples, frames, self.output_.begin());
    return ok ? GmfPcmPipeline::Result::Written : GmfPcmPipeline::Result::Failed;
  }

  void transform_samples(const int16_t *samples, int16_t *output, size_t frames) {
    const float gain=block_gain_q15_/32767.0F;
    if(!processor_->process_pcm(samples,output,frames,1,false,gain))std::fill_n(output,frames,int16_t(0));
  }

  void process_block(const int16_t *samples, size_t frames,
                     uint16_t gain_q15, uint32_t generation) {
    std::unique_lock<std::mutex> processing_lock(processing_mutex_);
    auto &profile=output_profile_;profile.begin();
    output_cadence_.observe(profile.active() && samples!=nullptr,
        control_generation_.load(std::memory_order_acquire),[]{return uint64_t(esp_timer_get_time());});
    block_generation_ = generation;
    block_gain_q15_ = gain_q15;
    silence_block_ = samples == nullptr;
    transform_ran_ = false;
    block_write_us_ = 0;
    block_write_started_us_ = block_write_generation_ = block_write_gap_ = 0;
    const bool opened = pipeline_.is_open() || pipeline_.open(
        output_sample_rate(), transform_callback, writer_callback, this);
    const auto result = opened ? pipeline_.write(samples, frames) : GmfPcmPipeline::Result::Failed;
    const bool ok = result == GmfPcmPipeline::Result::Written;
    const bool failed = result == GmfPcmPipeline::Result::Failed;
    if (failed) {
      // A vendor IO fault poisons the stream. Recreate it on the next block;
      // even an allocation/open fault must let the existing fade retire.
      (void)pipeline_.close();
      if (!transform_ran_) transform_samples(samples, output_.data(), frames);
    }
    const uint32_t elapsed = block_write_us_;
    const auto started = block_write_started_us_;
    const auto write_generation = block_write_generation_;
    const auto write_gap = block_write_gap_;
    profile.mark(1);
    const auto dsp_status = processor_->diagnostics();
    const bool muted = processor_->muted();
    const auto sample_rate = output_sample_rate();
    processing_lock.unlock();
    std::unique_lock<std::mutex> lock(status_mutex_);
    status_.maximum_write_us = std::max(status_.maximum_write_us, elapsed);
    if(samples!=nullptr&&started&&write_generation_!=write_generation){
      write_generation_=write_generation;
      status_.maximum_write_start_gap_us=0;status_.maximum_write_start_gap_at_us=0;
    }
    if(samples!=nullptr&&started&&write_gap>status_.maximum_write_start_gap_us){
      status_.maximum_write_start_gap_us=uint32_t(std::min<uint64_t>(write_gap,UINT32_MAX));
      status_.maximum_write_start_gap_at_us=uint64_t(started);
    }
    if (samples != nullptr) status_.pending_frames -= static_cast<uint32_t>(frames);
    if (ok) {
      if (samples != nullptr) status_.completed_frames += static_cast<uint32_t>(frames);
    } else {
      if (failed) ++status_.output_write_failures;
      if (samples != nullptr) status_.discarded_frames += static_cast<uint32_t>(frames);
    }
    status_.output_muted = muted;
    status_.input_peak = dsp_status.input_peak;
    status_.output_peak = dsp_status.output_peak;
    status_.rms = dsp_status.rms;
    status_.limiter_gain = dsp_status.limiter_gain;
    status_.protection_gain = dsp_status.protection_gain;
    status_.limiter_events = dsp_status.limiter_events;
    status_.soft_clip_samples = dsp_status.soft_clip_samples;
    const auto observer = output_observer_;
    lock.unlock();
    // Never invoke the bridge while holding the diagnostics mutex: status
    // snapshots acquire the bridge mutex before reading these diagnostics.
    if (ok && observer) observer(output_.data(), frames, sample_rate);
    profile.mark(2);
    const auto measured=profile.finish();
    if(measured.calls){
      ESP_LOGI(kTag,"OUTPUT_PROFILE calls=%u window_us=%llu worker_task_us=%u dsp_us=%llu write_us=%llu reference_us=%llu",
        unsigned(measured.calls),static_cast<unsigned long long>(measured.window_us),unsigned(measured.worker_task_us),static_cast<unsigned long long>(measured.wall[0]),static_cast<unsigned long long>(measured.wall[1]),static_cast<unsigned long long>(measured.wall[2]));
      ESP_LOGI(kTag,"OUTPUT_MAX block_start_gap_us=%llu dsp_us=%u write_us=%u reference_us=%u ready_blocks=%u queue_age_max_us=%llu empty_wait_max_us=%llu",
          static_cast<unsigned long long>(output_cadence_.take_maximum()),unsigned(measured.maximum_us[0]),
          unsigned(measured.maximum_us[1]),unsigned(measured.maximum_us[2]),unsigned(uxQueueMessagesWaiting(ready_queue_)),
          static_cast<unsigned long long>(maximum_queue_age_us_),static_cast<unsigned long long>(maximum_empty_wait_us_));
      maximum_queue_age_us_=maximum_empty_wait_us_=0;
    }
    if (failed) {
      std::lock_guard<std::mutex> producer_lock(producer_mutex_);
      // A failure from an old in-flight write cannot cancel a newer owner.
      if (generation != control_generation_.load(std::memory_order_acquire)) return;
      const bool was_requested=output_requested_.exchange(false, std::memory_order_acq_rel);
      mute_target_.store(true, std::memory_order_release);
      // A failed silence write during retirement must not restart the fade
      // forever. The first failure closes admission; subsequent failures let
      // the existing ramp finish and switch off hardware.
      if(was_requested)control_generation_.fetch_add(1, std::memory_order_acq_rel);
      drain_ready_queue();
    }
  }

  void drain_ready_queue() {
    if (ready_queue_ == nullptr || free_queue_ == nullptr) return;
    uint8_t index = 0;
    while (xQueueReceive(ready_queue_, &index, 0) == pdTRUE) {
      {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.pending_frames -= blocks_[index].frames;
        status_.discarded_frames += blocks_[index].frames;
      }
      (void)xQueueSend(free_queue_, &index, 0);
    }
  }

public:
  bool quiesce(std::string &error) override {
    const bool input_stopped = enable_input(false);
    // Repeated retirement must not restart the existing fade/generation.
    if (output_requested_.load(std::memory_order_acquire) ||
        paused_.load(std::memory_order_acquire) || !mute_target_.load(std::memory_order_acquire))
      (void)enable_output(false);
    if (!input_stopped) { error="audio_device_stop_failed"; return false; }
    std::lock_guard<std::mutex> producer_lock(producer_mutex_);
    if (hardware_output_enabled_.load(std::memory_order_acquire)) {
      wake_locked(); error="audio_foreground_stop_pending"; return false;
    }
    std::lock_guard<std::mutex> status_lock(status_mutex_);
    if (status_.pending_frames) { error="audio_foreground_stop_pending"; return false; }
    error.clear(); return true;
  }
  bool shutdown(std::string &error) override {
    {
      std::lock_guard<std::mutex> producer_lock(producer_mutex_);
      closing_=true;
      stop_.store(true, std::memory_order_release); wake_locked();
    }
    if (task_ != nullptr) {
      if (!exited_.load(std::memory_order_acquire) ||
          (runtime_ && runtime_->task_active("audio.realtime_pcm"))) {
        error = "realtime_pcm_stop_pending"; return false;
      }
      task_ = nullptr;
    }
    if (hardware_output_enabled_.exchange(false,
                                          std::memory_order_acq_rel) &&
        hardware_ != nullptr) {
      (void)hardware_->enable_output(false);
    }
    if(hardware_ && !hardware_->shutdown(error))return false;
    if (!pipeline_.close()) { error="realtime_pcm_render_close_pending"; return false; }
    if (ready_queue_ != nullptr) {
      vQueueDelete(ready_queue_);
      ready_queue_ = nullptr;
    }
    if (free_queue_ != nullptr) {
      vQueueDelete(free_queue_);
      free_queue_ = nullptr;
    }
    error.clear(); return true;
  }
private:
  void shutdown() { std::string error; (void)shutdown(error); }
  lunaredu::lunaros::KernelRuntime *runtime_ = nullptr;
  std::string owner_service_id_;
  bool closing_=false,queues_seeded_=false;
  std::unique_ptr<PcmBackend> hardware_;
  std::array<AudioBlock, kBlockCount> blocks_{};
  std::array<int16_t, kFramesPerBlock> output_{};
  lunaredu::system::RuntimeWorkProfile<3> output_profile_;
  OutputCadence output_cadence_;
  OutputCadence write_cadence_;
  uint64_t write_generation_=0;
  std::atomic<uint32_t> write_pause_revision_{0};
  QueueHandle_t free_queue_ = nullptr;
  QueueHandle_t ready_queue_ = nullptr;
  lunaredu::lunaros::ManagedTaskHandle task_ = nullptr;
  std::atomic<bool> stop_{false};
  std::atomic<bool> exited_{true};
  std::atomic<bool> output_requested_{false};
  std::atomic<bool> input_requested_{false};
  uint64_t prepared_at_us_=0; // producer_mutex_ protected
  std::atomic<bool> paused_{false};
  std::atomic<bool> mute_target_{true};
  std::atomic<bool> hardware_output_enabled_{false};
  std::atomic<uint32_t> control_generation_{0};
  internal::RealtimeDspStorage processor_;
  GmfPcmPipeline pipeline_;
  uint32_t block_generation_ = 0, block_write_us_ = 0;
  uint64_t block_write_started_us_ = 0, block_write_generation_ = 0, block_write_gap_ = 0;
  uint16_t block_gain_q15_ = 32767;
  bool silence_block_ = false, transform_ran_ = false;
  uint64_t empty_since_us_=0, maximum_queue_age_us_=0, maximum_empty_wait_us_=0;
  std::mutex producer_mutex_;
  std::mutex processing_mutex_;
  mutable std::mutex status_mutex_;
  PcmBackendDiagnostics status_{.realtime = true};
  OutputObserver output_observer_;
};

} // namespace

std::unique_ptr<PcmBackend> create_realtime_pcm_backend(
    std::unique_ptr<PcmBackend> hardware,
    lunaredu::lunaros::KernelRuntime &runtime,
    const char *owner_service_id, std::string &error) {
  auto backend = std::unique_ptr<RealtimePcmBackend>(
      new (std::nothrow) RealtimePcmBackend(std::move(hardware)));
  if (backend == nullptr) {
    error = "realtime_pcm_backend_allocation_failed";
    return nullptr;
  }
  if (!backend->initialize(runtime, owner_service_id, error)) return nullptr;
  return backend;
}

} // namespace lunaredu::audio
