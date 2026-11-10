#pragma once
#include <cstdint>
namespace lunaredu::audio {
// Bind before the first volume read, after OS configuration is initialized.
// The callback must not re-enter this API. Persistence retains the deployed key.
struct VolumeOutputPorts {
  bool (*set_volume)(uint8_t) = nullptr;
  // Used only when the persisted setting is absent or invalid. Products can
  // choose a safe acoustic default without replacing a user's saved choice.
  uint8_t default_volume_percent = 70;
};
void configure_volume_output(VolumeOutputPorts ports);
uint8_t volume_percent();
// A failed persistence/output attempt returns false but keeps the requested
// runtime value; apply_volume() retries it after output recovery.
bool set_volume_percent(uint8_t percent);
// Durable intent can be saved before output exists. The caller must distinguish
// successful storage from the separate applied acknowledgement.
bool save_volume_percent(uint8_t percent, bool &applied);
// Reapply after the Device audio backend is initialized or restarted.
bool apply_volume();
}
