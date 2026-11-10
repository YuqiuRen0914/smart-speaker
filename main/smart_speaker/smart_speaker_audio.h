#pragma once
#include "lunaros_audio_dual_i2s.h"
namespace smart_speaker {
namespace dsp=lunaredu::audio::dsp;
using namespace lunaredu::audio;
DualI2sConfig speaker_audio_config();
bool setup_speaker_audio_output(lunaredu::lunaros::KernelRuntime &);
void smart_speaker_quiet_audio_pins_early();
bool smart_speaker_validate_and_log_gpio_ownership();
}
