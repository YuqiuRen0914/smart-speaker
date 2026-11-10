#include "lunaredu_music_lyrics.h"
#include <cassert>
#include "lunaros_audio_pcm_clock.h"
using lunaredu::audio::PcmClock;
#include <iostream>
using namespace lunaredu::music::lyrics;
int main() {
  Document d; std::string error;
  assert(parse("\xef\xbb\xbf[ar:测试]\r\n[offset:100]\r\n[00:02.1][00:04.123]窗外落下一束光\r\n[00:01.00]现在开始\n[00:03.00]\n",d,error));
  assert(d.cues.size()==4 && d.cues[1].ms==2100);
  assert(d.line(899).empty()); assert(d.line(900)=="现在开始");
  assert(d.line(2000)=="窗外落下一束光"); assert(d.line(2900).empty());
  assert(d.line(900,500).empty()); assert(d.line(400,-500)=="现在开始");
  assert(parse("[00:01]one\n[00:01]一\n[00:02]\n",d,error));
  assert(d.line(1000)=="one\n一" && d.line(2000).empty());
  assert(!parse("plain text",d,error) && d.cues.empty());
  assert(!parse("[00:70.00]bad",d,error));
  assert(!parse("[00:01.1234]bad",d,error));
  assert(!parse(std::string("[00:01]\xc0\xaf"),d,error));
  assert(!parse(std::string("[00:01]\xed\xa0\x80"),d,error));
  assert(!parse(std::string(kMaxBytes+1,'a'),d,error));
  assert(!parse("[offset:9999999]\n[00:01]bad",d,error));
  std::string too_many; for(size_t i=0;i<=kMaxCues;++i) too_many+="[00:01]x\n";
  assert(!parse(too_many,d,error));
  assert(!parse("[00:01]"+std::string(513,'a'),d,error));
  std::string expansion; for(int i=0;i<200;++i) expansion+="[00:01]";
  assert(!parse(expansion+std::string(512,'x'),d,error));
  PcmClock clock;
  for(int i=0;i<44100;++i) clock.advance(128,44100);
  assert(clock.milliseconds()==128000);
  const auto paused=clock.milliseconds(); assert(paused==128000);
  for(int i=0;i<48000;++i) clock.advance(128,48000);
  assert(clock.milliseconds()==256000);
  clock.reset(); assert(clock.milliseconds()==0);
  clock.advance(48000,48000); assert(clock.milliseconds()==1000);
  // Private host fixture: replacement and interrupted rename recovery.
  const std::string track="speaker-lyrics-host-fixture.mp3", path=path_for(track);
  assert(save(path,"[00:01]first\n")); assert(load(track)->line(1000)=="first");
  assert(save(path,"[00:02]second\n")); assert(load(track)->line(2000)=="second");
  assert(rename(path.c_str(),(path+".bak").c_str())==0);
  assert(load(track)->line(2000)=="second");
  assert(save(path,"[00:03]third\n")); assert(load(track)->line(3000)=="third");
  assert(save(path+".offset","-500")); assert(delay(track)==-500);
  remove(path.c_str()); remove((path+".offset").c_str());
  std::cout<<"lyrics parser, PCM clock and storage tests passed\n";
}
