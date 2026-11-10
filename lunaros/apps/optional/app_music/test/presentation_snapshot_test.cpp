#include "lunaredu_music_presentation_snapshot.h"
#include "lunaredu_music_actions.h"
#include <cassert>
#include <cstring>
#include <iostream>
namespace {lunaredu::music::MusicSnapshot state;unsigned local=0,speaker=0,selected=0;}
namespace lunaredu::music {
MusicSnapshot music_snapshot(){return state;}
bool local_action(uint32_t,std::string &e){++local;e.clear();return true;}
bool speaker_action(uint32_t,const SpeakerSnapshot &,std::string &e){++speaker;e.clear();return true;}
bool select_track(size_t i,std::string &e){selected=unsigned(i)+1;e.clear();return true;}
bool select_track_snapshot(const MusicSnapshot &,size_t i,std::string &e){return select_track(i,e);}
}
int main(){
  using namespace lunaredu::music;PresentationSnapshotCache cache;
  const auto first=cache.capture("");assert(cache.capture("")==first);
  state.playing=true;const auto changed=cache.capture("");assert(changed!=first&&!first->playing);
  const auto error=cache.capture("input refused");assert(error!=changed&&!std::strcmp(error->error.data(),"input refused"));
  assert(!changed->error[0]&&cache.capture("input refused")==error);assert(cache.capture("")!=error);
  std::string e;assert(submit_action(218,state,e)&&local==1&&speaker==0);
  assert(submit_action(222,state,e)&&speaker==1);
  state.library=std::make_shared<const Library>(Library{{"a.mp3","A",true},{"b.mp3","B",true}});
  assert(submit_action(5001,state,e)&&selected==2);
  state.speaker.receiving=true;assert(submit_action(218,state,e)&&speaker==2&&local==1);
  assert(submit_action(220,state,e)&&local==2); // Volume is the device's own.
  assert(!submit_action(5001,state,e)&&e=="music_stop_bluetooth_first"&&selected==2);
  std::cout<<"PASS Music immutable local-first snapshots and optional speaker action routing\n";
}

