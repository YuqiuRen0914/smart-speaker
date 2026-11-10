#pragma once

#include <cstdint>
#include <string>

namespace lunaredu::pet::conversation {

struct AssistantPolicySnapshot {
  bool desired_enabled = true;
  bool microphone_enabled = true;
  bool temporarily_suspended = false;
  std::string audio_focus_owner;
  std::string suspension_reason;
};

// Loads the user preference before the XiaoZhi resident service is created.
void setup_assistant_policy();
bool assistant_desired_enabled();
bool assistant_microphone_enabled();

// Persists user intent. This is the setting exposed by LunarOS Settings.
bool set_assistant_desired_enabled(bool enabled);
bool set_assistant_microphone_enabled(bool enabled);
void on_assistant_enabled_changed(void *context, bool enabled);

// Called by the XiaoZhi service before it starts listening or playback.
bool request_assistant_audio_focus(void *context, const char *reason,
                                   uint32_t timeout_ms);

AssistantPolicySnapshot assistant_policy_snapshot();

} // namespace lunaredu::pet::conversation
