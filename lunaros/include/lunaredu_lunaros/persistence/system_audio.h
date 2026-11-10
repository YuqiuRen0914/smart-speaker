#pragma once
#include <cstdint>
namespace lunaredu::lunaros {
// Product-owned backend binding, installed before App services start.
// The callback must not re-enter this API. Settings retain the deployed key.
struct AudioOutputPorts { bool (*set_volume)(uint8_t) = nullptr; };
void configure_audio_output(AudioOutputPorts ports);
uint8_t system_volume_percent();
bool set_system_volume_percent(uint8_t percent);
// Reapply after the Device audio backend is initialized or restarted.
bool apply_system_volume();
}
