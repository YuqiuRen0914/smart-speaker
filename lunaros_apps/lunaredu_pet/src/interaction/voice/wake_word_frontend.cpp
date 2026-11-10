#include "lunaredu_lunaros/delivery/managed_assets.h"
#include "lunaredu_lunaros/persistence/storage_pool.h"
#include "interaction/voice/wake_word_frontend.h"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>
#include <sstream>
#include <utility>

#include <esp_afe_sr_models.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_nsn_models.h>
#include <esp_vad.h>
#include <esp_wn_iface.h>
#include <esp_wn_models.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <model_path.h>

namespace lunaredu::pet::conversation {

namespace {

constexpr const char *kTag = "wake_word_frontend";
constexpr int kSampleRate = 16000;
constexpr int kChannels = 1;
constexpr uint32_t kFrameDurationMs = 60;
constexpr uint32_t kInitialFetchWarmupChunks = 8;
constexpr uint32_t kFetchWaitMs = 10;

void split_wake_words(const char *words, std::vector<std::string> &output) {
  output.clear();
  if (words == nullptr || words[0] == '\0') {
    return;
  }

  std::stringstream stream(words);
  std::string word;
  while (std::getline(stream, word, ';')) {
    if (!word.empty()) {
      output.push_back(std::move(word));
    }
  }
}

const esp_afe_sr_iface_t *as_afe_iface(const void *value) {
  return static_cast<const esp_afe_sr_iface_t *>(value);
}

esp_afe_sr_data_t *as_afe_data(void *value) {
  return static_cast<esp_afe_sr_data_t *>(value);
}

srmodel_list_t *as_models(void *value) {
  return static_cast<srmodel_list_t *>(value);
}

std::string normalize_wake_word(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) {
                               return ch == ' ' || ch == '\t' || ch == '\r' ||
                                      ch == '\n';
                             }),
              value.end());
  return value;
}

} // namespace

WakeWordFrontend::WakeWordFrontend(std::string model_partition_label)
    : model_partition_label_(std::move(model_partition_label)) {}

WakeWordFrontend::~WakeWordFrontend() { deinitialize(); }

bool WakeWordFrontend::ensure_initialized(
    bool wake_word_enabled, const std::string &requested_wake_word) {
  const bool wake_word_mode_compatible =
      wake_word_enabled_ == wake_word_enabled ||
      (wake_word_enabled_ && !wake_word_enabled);
  if (initialized_ && wake_word_mode_compatible &&
      requested_wake_word_ == requested_wake_word) {
    return true;
  }
  deinitialize();
  return initialize(wake_word_enabled, requested_wake_word);
}

void WakeWordFrontend::reset() {
  reset_buffers();
  if (initialized_) {
    as_afe_iface(afe_iface_)->reset_buffer(as_afe_data(afe_data_));
  }
}

void WakeWordFrontend::stop() {
  running_ = false;
  reset_buffers();
  if (initialized_) {
    as_afe_iface(afe_iface_)->reset_buffer(as_afe_data(afe_data_));
  }
  ESP_LOGI(kTag, "Wake word frontend stopped");
}

void WakeWordFrontend::start() {
  if (!initialized_) {
    ESP_LOGW(kTag, "Cannot start: not initialized");
    return;
  }
  reset_buffers();
  if (initialized_) {
    as_afe_iface(afe_iface_)->reset_buffer(as_afe_data(afe_data_));
  }
  running_ = true;
  ESP_LOGI(kTag, "Wake word frontend started");
}

bool WakeWordFrontend::process(std::vector<int16_t> &frame,
                               bool voice_processing_enabled,
                               bool wake_word_enabled,
                               const std::string &requested_wake_word,
                               WakeWordFrontendEvent &event) {
  if (model_data_ && model_generation_ != lunaredu::lunaros::managed_asset_generation("model")) {
    deinitialize();
    set_error("voice_model_changed_requires_reinit");
    return false;
  }
  if (!initialized_) {
    set_error("wake_word_not_initialized");
    return false;
  }
  if (!requested_wake_word.empty() &&
      requested_wake_word_ != requested_wake_word) {
    set_error("wake_word_request_changed_requires_reinit");
    return false;
  }
  if (frame.empty()) {
    return false;
  }

  input_frames_ += 1;
  last_input_samples_ = static_cast<uint32_t>(frame.size());
  if (device_aec_enabled_) {
    const size_t mic_samples = frame.size();
    const size_t ref_take = std::min(mic_samples, reference_buffer_.size());
    input_buffer_.reserve(input_buffer_.size() + mic_samples * 2);
    for (size_t index = 0; index < mic_samples; ++index) {
      input_buffer_.push_back(frame[index]);
      if (index < ref_take) {
        input_buffer_.push_back(reference_buffer_[index]);
      } else {
        input_buffer_.push_back(0);
      }
    }
    if (ref_take > 0) {
      reference_buffer_.erase(reference_buffer_.begin(),
                              reference_buffer_.begin() + ref_take);
    }
  } else {
    input_buffer_.insert(input_buffer_.end(), frame.begin(), frame.end());
  }

  const auto *iface = as_afe_iface(afe_iface_);
  auto *data = as_afe_data(afe_data_);
  feed_chunk_samples_ =
      static_cast<uint32_t>(std::max(1, iface->get_feed_chunksize(data)));
  feed_channel_count_ =
      static_cast<uint32_t>(std::max(1, iface->get_feed_channel_num(data)));
  fetch_chunk_samples_ =
      static_cast<uint32_t>(std::max(1, iface->get_fetch_chunksize(data)));
  const size_t feed_samples =
      static_cast<size_t>(feed_chunk_samples_) *
      static_cast<size_t>(feed_channel_count_);
  size_t fed_chunks = 0;
  while (input_buffer_.size() >= feed_samples) {
    iface->feed(data, input_buffer_.data());
    input_buffer_.erase(input_buffer_.begin(),
                        input_buffer_.begin() + feed_samples);
    feed_chunks_ += 1;
    fed_chunks += 1;
  }
  last_fed_chunks_ = static_cast<uint32_t>(fed_chunks);
  fed_chunks_since_reset_ += static_cast<uint32_t>(fed_chunks);
  buffered_input_samples_ = static_cast<uint32_t>(input_buffer_.size());

  const size_t buffered_feed_chunks =
      feed_samples == 0 ? 0 : (input_buffer_.size() / feed_samples);
  if (buffered_feed_chunks > 4) {
    feed_overflow_events_ += 1;
  }

  return process_fetch_results(frame, voice_processing_enabled,
                               wake_word_enabled, fed_chunks, event);
}

WakeWordFrontendStatus WakeWordFrontend::status() const {
  WakeWordFrontendStatus status;
  status.initialized = initialized_;
  status.running = running_;
  status.wake_word_enabled = wake_word_enabled_;
  status.device_aec_enabled = device_aec_enabled_;
  status.input_frames = input_frames_;
  status.output_frames = output_frames_;
  status.feed_chunks = feed_chunks_;
  status.fetch_failures = fetch_failures_;
  status.vad_state_changes = vad_state_changes_;
  status.wake_word_detections = wake_word_detections_;
  status.last_input_samples = last_input_samples_;
  status.last_output_samples = last_output_samples_;
  status.last_vad_state = last_vad_state_;
  status.last_error = last_error_;
  status.requested_wake_word = requested_wake_word_;
  status.wake_word_model_name = wake_word_model_name_;
  status.available_wake_words = wake_words_;
  status.requested_wake_word_available = requested_wake_word_available_;
  status.feed_overflow_events = feed_overflow_events_;
  status.standby_fetch_events = standby_fetch_events_;
  status.empty_fetch_skips = empty_fetch_skips_;
  status.last_fed_chunks = last_fed_chunks_;
  status.feed_chunk_samples = feed_chunk_samples_;
  status.feed_channel_count = feed_channel_count_;
  status.fetch_chunk_samples = fetch_chunk_samples_;
  status.buffered_input_samples = buffered_input_samples_;
  status.fed_chunks_since_reset = fed_chunks_since_reset_;
  return status;
}

bool WakeWordFrontend::initialize(bool wake_word_enabled,
                                  const std::string &requested_wake_word) {
  // The initializer runs with a 4 KiB internal-RAM task stack.  Once that
  // stack is reserved, the remaining largest block can be around 1.5 KiB on
  // the voice-enabled S3 build; ESP-SR's large AFE buffers are explicitly
  // allocated in PSRAM. Keep only a small control-allocation floor here and
  // let the ESP-SR allocators return a precise failure if they need more.
  constexpr size_t kMinimumInternalLargestBlock = 1024;
  const size_t internal_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (internal_largest < kMinimumInternalLargestBlock) {
    set_error("wake_word_insufficient_internal_memory");
    ESP_LOGW(kTag,
             "Wake frontend deferred: internal largest block=%u need=%u",
             static_cast<unsigned>(internal_largest),
             static_cast<unsigned>(kMinimumInternalLargestBlock));
    return false;
  }
  requested_wake_word_ = requested_wake_word;
  if (lunaredu::lunaros::storage_pool_enabled()) {
    size_t bytes = 0;
    std::string error;
    model_generation_ = lunaredu::lunaros::managed_asset_generation("model");
    model_data_ = lunaredu::lunaros::load_managed_asset("model", bytes, error);
    if (!model_data_) { set_error(error); return false; }
    models_ = srmodel_load(model_data_);
  } else {
    models_ = esp_srmodel_init(model_partition_label_.c_str());
  }
  auto *models = as_models(models_);
  if (models == nullptr || models->num <= 0) {
    set_error("wake_word_model_partition_empty");
    deinitialize();
    return false;
  }

  char *vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, nullptr);
  char *wake_model_name =
      wake_word_enabled ? esp_srmodel_filter(models, ESP_WN_PREFIX, nullptr)
                        : nullptr;
  wake_word_model_name_ =
      wake_model_name == nullptr ? std::string() : std::string(wake_model_name);
  char *wake_words = wake_model_name == nullptr
                         ? nullptr
                         : esp_srmodel_get_wake_words(models, wake_model_name);
  split_wake_words(wake_words, wake_words_);
  if (wake_words != nullptr) {
    std::free(wake_words);
  }
  requested_wake_word_available_ =
      has_requested_wake_word(requested_wake_word_);

  afe_config_t *config = afe_config_init(
      "M", models, wake_word_enabled ? AFE_TYPE_SR : AFE_TYPE_VC,
      // Keep enough internal SRAM available for Wi-Fi/TLS while the always-on
      // wake frontend is resident. High-performance AFE is not sustainable on
      // this 8 MB PSRAM board alongside the XiaoZhi MQTT and device-link
      // stacks; the low-cost pipeline still provides VAD/AEC/wake detection.
      AFE_MODE_LOW_COST);
  if (config == nullptr) {
    set_error("wake_word_afe_config_init_failed");
    deinitialize();
    return false;
  }

  // Capture is deliberately half-duplex in the assistant loop: it is stopped
  // while downlink speech or the wake chime is playing. Feeding an all-zero
  // reference into AEC therefore cannot cancel real echo, but it does keep the
  // ESP32-S3 core almost continuously busy and starves the idle watchdog/UI.
  // Use the one-microphone pipeline and leave AEC out of the resident frontend.
  config->aec_init = false;
  config->vad_init = true;
  config->vad_mode = VAD_MODE_3;
  config->vad_min_speech_ms = 120;
  config->vad_min_noise_ms = 900;
  if (vad_model_name != nullptr) {
    config->vad_model_name = vad_model_name;
  }
  // The 10 ms resident WakeNet path already performs its own feature
  // extraction. Running WebRTC NS ahead of it keeps CPU1 continuously busy on
  // this product and prevents the idle watchdog/network stack from running.
  // Conversation audio bypasses this frontend, so disable the redundant NS.
  config->ns_init = false;
  config->wakenet_init = wake_model_name != nullptr;
  config->wakenet_model_name = wake_model_name;
  config->wakenet_mode = DET_MODE_90;
  config->agc_init = false;
  config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

  afe_iface_ = esp_afe_handle_from_config(config);
  if (afe_iface_ == nullptr) {
    set_error("wake_word_afe_handle_create_failed");
    deinitialize();
    return false;
  }
  const size_t before_data_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (before_data_largest < kMinimumInternalLargestBlock) {
    set_error("wake_word_insufficient_internal_memory");
    ESP_LOGW(kTag,
             "Wake frontend data creation deferred: internal largest block=%u need=%u",
             static_cast<unsigned>(before_data_largest),
             static_cast<unsigned>(kMinimumInternalLargestBlock));
    deinitialize();
    return false;
  }
  afe_data_ = as_afe_iface(afe_iface_)->create_from_config(config);
  if (afe_data_ == nullptr) {
    set_error("wake_word_afe_data_create_failed");
    deinitialize();
    return false;
  }

  initialized_ = true;
  running_ = true;
  wake_word_enabled_ = wake_word_enabled && wake_model_name != nullptr;
  device_aec_enabled_ = false;
  feed_chunk_samples_ =
      static_cast<uint32_t>(std::max(
          1, as_afe_iface(afe_iface_)->get_feed_chunksize(
                 as_afe_data(afe_data_))));
  feed_channel_count_ =
      static_cast<uint32_t>(std::max(
          1, as_afe_iface(afe_iface_)->get_feed_channel_num(
                 as_afe_data(afe_data_))));
  fetch_chunk_samples_ =
      static_cast<uint32_t>(std::max(
          1, as_afe_iface(afe_iface_)->get_fetch_chunksize(
                 as_afe_data(afe_data_))));
  set_error("");
  ESP_LOGI(
      kTag,
      "Wake frontend ready: wake_word=%s model=%s requested=%s matched=%s device_aec=%s feed=%" PRIu32 " channels=%" PRIu32 " feed_samples=%" PRIu32 " fetch=%" PRIu32,
      wake_word_enabled_ ? "true" : "false",
      wake_word_model_name_.empty() ? "-" : wake_word_model_name_.c_str(),
      requested_wake_word_.empty() ? "-" : requested_wake_word_.c_str(),
      requested_wake_word_available_ ? "true" : "false",
      device_aec_enabled_ ? "true" : "false",
      feed_chunk_samples_, feed_channel_count_,
      feed_chunk_samples_ * feed_channel_count_, fetch_chunk_samples_);
  return true;
}

void WakeWordFrontend::deinitialize() {
  if (afe_iface_ != nullptr && afe_data_ != nullptr) {
    as_afe_iface(afe_iface_)->destroy(as_afe_data(afe_data_));
  }
  afe_iface_ = nullptr;
  afe_data_ = nullptr;
  if (models_ != nullptr) {
    esp_srmodel_deinit(as_models(models_));
  }
  models_ = nullptr;
  heap_caps_free(model_data_);
  model_data_ = nullptr;
  wake_words_.clear();
  wake_word_model_name_.clear();
  requested_wake_word_.clear();
  initialized_ = false;
  running_ = false;
  wake_word_enabled_ = false;
  device_aec_enabled_ = false;
  requested_wake_word_available_ = false;
  reset_buffers();
}

void WakeWordFrontend::release() { deinitialize(); }

void WakeWordFrontend::reset_buffers() {
  input_buffer_.clear();
  reference_buffer_.clear();
  output_buffer_.clear();
  fed_chunks_since_reset_ = 0;
  standby_pending_feed_samples_ = 0;
  last_vad_state_ = -1;
}

bool WakeWordFrontend::feed_output_reference(const int16_t *samples,
                                             size_t sample_count) {
  if (!initialized_ || !device_aec_enabled_ || samples == nullptr ||
      sample_count == 0) {
    return false;
  }
  constexpr size_t kMaxReferenceSamples = static_cast<size_t>(kSampleRate) * 4;
  if (reference_buffer_.size() + sample_count > kMaxReferenceSamples) {
    const size_t overflow =
        (reference_buffer_.size() + sample_count) - kMaxReferenceSamples;
    if (overflow >= reference_buffer_.size()) {
      reference_buffer_.clear();
    } else {
      reference_buffer_.erase(reference_buffer_.begin(),
                              reference_buffer_.begin() + overflow);
    }
  }
  reference_buffer_.insert(reference_buffer_.end(), samples,
                           samples + sample_count);
  return true;
}

bool WakeWordFrontend::process_fetch_results(
    std::vector<int16_t> &frame, bool voice_processing_enabled,
    bool wake_word_enabled, size_t fed_chunks, WakeWordFrontendEvent &event) {
  const auto *iface = as_afe_iface(afe_iface_);
  auto *data = as_afe_data(afe_data_);
  const size_t target_samples =
      static_cast<size_t>(kSampleRate / 1000) *
      static_cast<size_t>(kFrameDurationMs);
  if (!voice_processing_enabled) {
    standby_pending_feed_samples_ += static_cast<uint32_t>(
        fed_chunks * static_cast<size_t>(feed_chunk_samples_));
    if (fed_chunks == 0 ||
        standby_pending_feed_samples_ < fetch_chunk_samples_) {
      empty_fetch_skips_ += 1;
      frame.clear();
      last_output_samples_ = 0;
      return false;
    }

    // Standby consumes only the latest VAD/WakeNet decision. The paced feed
    // loop above keeps the input ring drained; the single-microphone frontend
    // produces exactly one decision per 60 ms bridge frame.
    constexpr size_t fetch_rounds = 1;
    bool got_result = false;
    for (size_t index = 0; index < fetch_rounds; ++index) {
      afe_fetch_result_t *result =
          iface->fetch_with_delay(data, pdMS_TO_TICKS(kFetchWaitMs));
      if (result == nullptr) {
        break;
      }
      if (result->ret_value == ESP_FAIL) {
        fetch_failures_ += 1;
        break;
      }

      got_result = true;
      standby_fetch_events_ += 1;
      standby_pending_feed_samples_ =
          standby_pending_feed_samples_ > fetch_chunk_samples_
              ? standby_pending_feed_samples_ - fetch_chunk_samples_
              : 0;
      update_vad_event(result->vad_state, event);
      if (wake_word_enabled) {
        update_wake_word_event(result->wakeup_state, result->wake_word_index,
                               result->wakenet_model_index, event);
      }
    }
    frame.clear();
    last_output_samples_ = 0;
    return got_result;
  }

  if (output_buffer_.empty() &&
      fed_chunks_since_reset_ < kInitialFetchWarmupChunks) {
    empty_fetch_skips_ += 1;
    return false;
  }

  size_t fetch_rounds = 0;
  while (output_buffer_.size() < target_samples && fetch_rounds < fed_chunks) {
    afe_fetch_result_t *result =
        iface->fetch_with_delay(data, pdMS_TO_TICKS(kFetchWaitMs));
    if (result == nullptr) {
      break;
    }
    if (result->ret_value == ESP_FAIL) {
      fetch_failures_ += 1;
      break;
    }

    update_vad_event(result->vad_state, event);
    if (wake_word_enabled) {
      update_wake_word_event(result->wakeup_state, result->wake_word_index,
                             result->wakenet_model_index, event);
    }
    fetch_rounds += 1;

    const size_t result_samples =
        result->data_size <= 0 ? 0 : result->data_size / sizeof(int16_t);
    if (voice_processing_enabled && result->data != nullptr &&
        result_samples > 0) {
      output_buffer_.insert(output_buffer_.end(), result->data,
                            result->data + result_samples);
    }
  }

  if (output_buffer_.size() < target_samples) {
    if (fed_chunks == 0) {
      empty_fetch_skips_ += 1;
    }
    return false;
  }

  frame.assign(output_buffer_.begin(), output_buffer_.begin() + target_samples);
  output_buffer_.erase(output_buffer_.begin(),
                       output_buffer_.begin() + target_samples);
  output_frames_ += 1;
  last_output_samples_ = static_cast<uint32_t>(frame.size());
  return true;
}

void WakeWordFrontend::update_vad_event(int vad_state,
                                        WakeWordFrontendEvent &event) {
  if (vad_state == last_vad_state_) {
    if (vad_state == VAD_SPEECH || vad_state == AFE_VAD_SPEECH) {
      event.voice_activity_active = true;
    }
    return;
  }

  last_vad_state_ = vad_state;
  vad_state_changes_ += 1;
  if (vad_state == VAD_SPEECH || vad_state == AFE_VAD_SPEECH) {
    event.voice_activity_changed = true;
    event.voice_activity_active = true;
  } else if (vad_state == VAD_SILENCE || vad_state == AFE_VAD_SILENCE) {
    event.voice_activity_changed = true;
    event.voice_activity_active = false;
  }
}

void WakeWordFrontend::update_wake_word_event(
    int wakeup_state, int wake_word_index, int wakenet_model_index,
    WakeWordFrontendEvent &event) {
  if (wakeup_state != WAKENET_DETECTED) {
    return;
  }

  wake_word_detections_ += 1;
  int index = wake_word_index > 0 ? wake_word_index : wakenet_model_index;
  std::string word;
  if (index > 0 && static_cast<size_t>(index) <= wake_words_.size()) {
    word = wake_words_[static_cast<size_t>(index - 1)];
  } else if (!wake_word_model_name_.empty()) {
    word = wake_word_model_name_;
  } else {
    word = "wake_word";
  }

  last_wake_word_ = word;
  event.wake_word_detected = true;
  event.wake_word = std::move(word);
}

void WakeWordFrontend::set_error(std::string error) {
  last_error_ = std::move(error);
  if (!last_error_.empty()) {
    ESP_LOGW(kTag, "%s", last_error_.c_str());
  }
}

bool WakeWordFrontend::has_requested_wake_word(
    const std::string &requested_wake_word) const {
  if (requested_wake_word.empty()) {
    return false;
  }
  const std::string wanted = normalize_wake_word(requested_wake_word);
  for (const auto &word : wake_words_) {
    if (normalize_wake_word(word) == wanted) {
      return true;
    }
  }
  return false;
}

} // namespace lunaredu::pet::conversation
