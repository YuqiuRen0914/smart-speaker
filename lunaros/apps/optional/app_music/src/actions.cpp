#include "lunaredu_music_actions.h"

namespace lunaredu::music {
bool submit_action(uint32_t id,const MusicSnapshot &s,std::string &error) {
  if(id>=5000&&s.library&&size_t(id-5000)<s.library->size()) {
    if(s.speaker.receiving){error="music_stop_bluetooth_first";return false;}
    return select_track_snapshot(s,id-5000,error);
  }
  if(id==222)return speaker_action(id,s.speaker,error);
  if(s.speaker.receiving&&(id==218||id==219||id==223))return speaker_action(id,s.speaker,error);
  return local_action(id,error);
}
}
