#pragma once
#include <esp_err.h>
using i2s_slot_mode_t=int;
enum AUDIO_PLAYER_MUTE_SETTING { AUDIO_PLAYER_MUTE, AUDIO_PLAYER_UNMUTE };
enum audio_player_state_t { AUDIO_PLAYER_STATE_IDLE, AUDIO_PLAYER_STATE_PLAYING, AUDIO_PLAYER_STATE_PAUSE };
esp_err_t audio_player_resume();
esp_err_t audio_player_stop();
audio_player_state_t audio_player_get_state();
