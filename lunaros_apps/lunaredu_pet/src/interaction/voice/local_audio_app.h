#pragma once

#include <string>
#include "lunaredu_audio_bridge.h"

using LocalAudioStatus = lunaredu::audio::LocalPcmAudioBridgeStatus;

void setup_local_audio_app();
bool local_audio_app_play_wake_chime();

