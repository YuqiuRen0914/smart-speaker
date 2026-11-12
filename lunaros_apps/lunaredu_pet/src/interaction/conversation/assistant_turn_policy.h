#pragma once

#include <cstdint>
#include <cstring>

namespace lunaredu::pet::conversation::internal {

inline bool single_turn_mode(const char *mode) {
  return mode != nullptr &&
      (std::strcmp(mode, "manual") == 0 || std::strcmp(mode, "auto") == 0);
}

inline bool should_finish_single_turn_after_silence(
    const char *mode, bool utterance_started, uint32_t utterance_ms,
    uint32_t silence_ms, uint32_t minimum_speech_ms,
    uint32_t required_silence_ms) {
  return single_turn_mode(mode) && utterance_started &&
      utterance_ms >= minimum_speech_ms &&
      silence_ms >= required_silence_ms;
}

inline bool should_timeout_empty_single_turn(
    const char *mode, bool listening, int64_t started_at_ms,
    bool utterance_started, int64_t now_ms, uint32_t timeout_ms) {
  return listening && single_turn_mode(mode) && started_at_ms > 0 &&
      !utterance_started && now_ms >= started_at_ms &&
      now_ms - started_at_ms >= static_cast<int64_t>(timeout_ms);
}

inline const char *single_turn_stop_reason(const char *mode) {
  return mode != nullptr && std::strcmp(mode, "manual") == 0
      ? "manual_finish" : "auto_stop";
}

inline bool should_finish_single_turn_at_limit(
    const char *mode, bool listening, int64_t started_at_ms,
    int64_t now_ms, uint32_t limit_ms) {
  return listening && single_turn_mode(mode) && started_at_ms > 0 &&
      now_ms >= started_at_ms &&
      now_ms - started_at_ms >= static_cast<int64_t>(limit_ms);
}

inline bool should_resume_after_response(const char *mode,
                                         bool standby_allows_resume) {
  return mode != nullptr && std::strcmp(mode, "manual") != 0 &&
      standby_allows_resume;
}

}  // namespace lunaredu::pet::conversation::internal
