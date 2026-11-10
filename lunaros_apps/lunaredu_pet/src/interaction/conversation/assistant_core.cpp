#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/assistant_session.h"
#include "interaction/voice/assistant_audio.h"
#include "lunaredu_pet_conversation_policy.h"

namespace lunaredu::pet::conversation::internal {

Runtime::Runtime() : session_state(conversation.session_state) {}

Runtime g_runtime;
void refresh_wake_word_status_locked(const lunaredu::pet::conversation::WakeWordFrontendStatus &status) {
  g_runtime.wake_word.afe_initialized = status.initialized;
  g_runtime.wake_word.afe_last_error = status.last_error;
  if (!status.requested_wake_word.empty()) {
    g_runtime.wake_word.requested_word = status.requested_wake_word;
  }
  if (!status.wake_word_model_name.empty()) {
    g_runtime.wake_word.model_name = status.wake_word_model_name;
  }
  if (!status.available_wake_words.empty()) {
    g_runtime.wake_word.available_words = status.available_wake_words;
  }
  g_runtime.wake_word.requested_word_available =
      status.requested_wake_word_available;
}
void drain_downlink_audio_queue();

int64_t now_ms() {
  return esp_timer_get_time() / 1000;
}

uint32_t pcm_abs_i16(int16_t value) {
  return value == std::numeric_limits<int16_t>::min()
             ? 32768U
             : static_cast<uint32_t>(std::abs(static_cast<int>(value)));
}

int16_t clamp_pcm_i32(int32_t value) {
  if (value > std::numeric_limits<int16_t>::max()) {
    return std::numeric_limits<int16_t>::max();
  }
  if (value < std::numeric_limits<int16_t>::min()) {
    return std::numeric_limits<int16_t>::min();
  }
  return static_cast<int16_t>(value);
}

void apply_pcm_gain(std::vector<int16_t> &samples, int numerator,
                    int denominator) {
  if (samples.empty() || numerator <= 0 || denominator <= 0 ||
      numerator == denominator) {
    return;
  }
  for (auto &sample : samples) {
    const int32_t scaled =
        (static_cast<int32_t>(sample) * numerator) / denominator;
    sample = clamp_pcm_i32(scaled);
  }
}

void remove_pcm_dc_bias(std::vector<int16_t> &samples) {
  if (samples.empty()) {
    return;
  }
  int64_t sum = 0;
  for (const auto sample : samples) {
    sum += sample;
  }
  const int32_t mean =
      static_cast<int32_t>(sum / static_cast<int64_t>(samples.size()));
  if (mean == 0) {
    return;
  }
  for (auto &sample : samples) {
    sample = clamp_pcm_i32(static_cast<int32_t>(sample) - mean);
  }
}

void normalize_pcm_peak(std::vector<int16_t> &samples, uint32_t target_peak,
                        uint32_t min_peak, int max_gain_numerator,
                        int denominator) {
  if (samples.empty() || target_peak == 0 || max_gain_numerator <= 0 ||
      denominator <= 0) {
    return;
  }
  uint32_t peak = 0;
  for (const auto sample : samples) {
    peak = std::max(peak, pcm_abs_i16(sample));
  }
  if (peak < min_peak || peak == 0) {
    return;
  }
  int numerator = static_cast<int>((target_peak * denominator) / peak);
  if (numerator <= denominator) {
    return;
  }
  numerator = std::min(numerator, max_gain_numerator);
  apply_pcm_gain(samples, numerator, denominator);
}

std::string heap_snapshot_text() {
  char buffer[192] = {};
  std::snprintf(
      buffer, sizeof(buffer),
      "heap_free=%u heap_min=%u sram_free=%u sram_min=%u sram_largest=%u psram_free=%u psram_largest=%u",
      static_cast<unsigned>(esp_get_free_heap_size()),
      static_cast<unsigned>(esp_get_minimum_free_heap_size()),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
  return buffer;
}

const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_UNKNOWN:
    return "unknown";
  case ESP_RST_POWERON:
    return "poweron";
  case ESP_RST_EXT:
    return "external";
  case ESP_RST_SW:
    return "software";
  case ESP_RST_PANIC:
    return "panic";
  case ESP_RST_INT_WDT:
    return "interrupt_watchdog";
  case ESP_RST_TASK_WDT:
    return "task_watchdog";
  case ESP_RST_WDT:
    return "other_watchdog";
  case ESP_RST_DEEPSLEEP:
    return "deepsleep";
  case ESP_RST_BROWNOUT:
    return "brownout";
  case ESP_RST_SDIO:
    return "sdio";
  case ESP_RST_USB:
    return "usb";
#ifdef ESP_RST_JTAG
  case ESP_RST_JTAG:
    return "jtag";
#endif
  default:
    return "other";
  }
}



const char *standby_listen_mode_name(StandbyListenMode mode) {
  switch (mode) {
  case StandbyListenMode::ContinuousConversation:
    return "continuous_conversation";
  case StandbyListenMode::WakeWord:
  default:
    return "wake_word";
  }
}

bool conversation_auto_resume_enabled_locked() {
  return lunaredu::pet::conversation::assistant_microphone_enabled() &&
         (g_runtime.conversation.listening_mode == "auto" ||
         g_runtime.conversation.listening_mode == "realtime" ||
         g_runtime.conversation.standby_mode ==
             StandbyListenMode::ContinuousConversation);
}

bool send_service_text_locked(const std::string &message);
} // namespace lunaredu::pet::conversation::internal
