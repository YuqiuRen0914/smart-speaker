#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lunaredu::pet::conversation {

struct WakeWordFrontendEvent {
  bool wake_word_detected = false;
  std::string wake_word;
  bool voice_activity_changed = false;
  bool voice_activity_active = false;
};

struct WakeWordFrontendStatus {
  bool initialized = false;
  bool running = false;
  bool wake_word_enabled = false;
  bool device_aec_enabled = false;
  uint32_t input_frames = 0;
  uint32_t output_frames = 0;
  uint32_t feed_chunks = 0;
  uint32_t fetch_failures = 0;
  uint32_t vad_state_changes = 0;
  uint32_t wake_word_detections = 0;
  uint32_t last_input_samples = 0;
  uint32_t last_output_samples = 0;
  int last_vad_state = -1;
  std::string last_error;
  std::string requested_wake_word;
  std::string wake_word_model_name;
  std::vector<std::string> available_wake_words;
  bool requested_wake_word_available = false;
  uint32_t feed_overflow_events = 0;
  uint32_t standby_fetch_events = 0;
  uint32_t empty_fetch_skips = 0;
  uint32_t last_fed_chunks = 0;
  uint32_t feed_chunk_samples = 0;
  uint32_t feed_channel_count = 0;
  uint32_t fetch_chunk_samples = 0;
  uint32_t buffered_input_samples = 0;
  uint32_t fed_chunks_since_reset = 0;
};

class WakeWordFrontend {
public:
  explicit WakeWordFrontend(std::string model_partition_label = "model");
  ~WakeWordFrontend();

  WakeWordFrontend(const WakeWordFrontend &) = delete;
  WakeWordFrontend &operator=(const WakeWordFrontend &) = delete;

  bool ensure_initialized(bool wake_word_enabled,
                          const std::string &requested_wake_word);
  void release();
  void reset();
  void stop();
  void start();
  bool is_running() const { return running_; }
  bool process(std::vector<int16_t> &frame, bool voice_processing_enabled,
               bool wake_word_enabled,
               const std::string &requested_wake_word,
               WakeWordFrontendEvent &event);
  bool feed_output_reference(const int16_t *samples, size_t sample_count);
  WakeWordFrontendStatus status() const;

private:
  bool initialize(bool wake_word_enabled,
                  const std::string &requested_wake_word);
  void deinitialize();
  void reset_buffers();
  bool process_fetch_results(std::vector<int16_t> &frame,
                             bool voice_processing_enabled,
                             bool wake_word_enabled,
                             size_t fed_chunks,
                             WakeWordFrontendEvent &event);
  void update_vad_event(int vad_state, WakeWordFrontendEvent &event);
  void update_wake_word_event(int wakeup_state, int wake_word_index,
                              int wakenet_model_index,
                              WakeWordFrontendEvent &event);
  void set_error(std::string error);
  bool has_requested_wake_word(const std::string &requested_wake_word) const;

  std::string model_partition_label_;
  void *models_ = nullptr;
  void *model_data_ = nullptr;
  uint32_t model_generation_ = 0;
  const void *afe_iface_ = nullptr;
  void *afe_data_ = nullptr;
  std::vector<int16_t> input_buffer_;
  std::vector<int16_t> reference_buffer_;
  std::vector<int16_t> output_buffer_;
  std::vector<std::string> wake_words_;
  std::string wake_word_model_name_;
  std::string last_wake_word_;
  std::string last_error_;
  std::string requested_wake_word_;
  uint32_t input_frames_ = 0;
  uint32_t output_frames_ = 0;
  uint32_t feed_chunks_ = 0;
  uint32_t fetch_failures_ = 0;
  uint32_t vad_state_changes_ = 0;
  uint32_t wake_word_detections_ = 0;
  uint32_t last_output_samples_ = 0;
  uint32_t last_input_samples_ = 0;
  uint32_t feed_overflow_events_ = 0;
  uint32_t standby_fetch_events_ = 0;
  uint32_t empty_fetch_skips_ = 0;
  uint32_t last_fed_chunks_ = 0;
  uint32_t feed_chunk_samples_ = 0;
  uint32_t feed_channel_count_ = 0;
  uint32_t fetch_chunk_samples_ = 0;
  uint32_t buffered_input_samples_ = 0;
  uint32_t fed_chunks_since_reset_ = 0;
  uint32_t standby_pending_feed_samples_ = 0;
  int last_vad_state_ = -1;
  bool initialized_ = false;
  bool running_ = false;
  bool wake_word_enabled_ = false;
  bool device_aec_enabled_ = false;
  bool requested_wake_word_available_ = false;
};

} // namespace lunaredu::pet::conversation
