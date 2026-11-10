#pragma once
#include "lunaredu_music_player.h"
namespace lunaredu::music {
bool submit_action(uint32_t id,const MusicSnapshot &s,std::string &error);
}
