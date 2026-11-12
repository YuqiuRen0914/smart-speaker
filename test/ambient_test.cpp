#include "smart_speaker_ambient.h"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
using namespace smart_speaker::ambient;
int main() {
  Session session;
  assert(session.choosing && session.scene == -1 && session.shown_scene() == 0);
  assert(!session.set_timer(1,100) && !session.timer_duration);
  assert(session.set_timer(30,100));
  session.scene=2; session.choosing=false;
  assert(session.remaining(90100)==1710000);
  assert(!session.expired(1800099) && session.expired(1800100));
  // Pausing, a voice lease, navigation and scene selection never alter this clock.
  session.scene=3; assert(session.expired(1800100));
  session.set_timer(90,UINT32_MAX-1000);
  assert(session.remaining(999)==5400000-2000);
  session.reveal(UINT32_MAX-1000); assert(session.visible(999)); assert(!session.visible(3000));
  session.timer_menu=true;assert(session.visible(9000));
  session.cancel_timer();assert(!session.expired(9000000) && !session.timer_menu);
  const auto path=std::filesystem::temp_directory_path()/"speaker-ambient-test.wav";
  std::array<uint8_t,44> header{};
  auto put=[&](int at,uint32_t value,int bytes=4){for(int i=0;i<bytes;++i)header[at+i]=uint8_t(value>>(i*8));};
  const unsigned count=88200;
  memcpy(header.data(),"RIFF",4);put(4,count+36);memcpy(header.data()+8,"WAVEfmt ",8);
  put(16,16);put(20,1,2);put(22,2,2);put(24,22050);put(28,88200);put(32,4,2);put(34,16,2);
  memcpy(header.data()+36,"data",4);put(40,count);
  std::vector<uint8_t> pcm(count);
  for(unsigned i=0;i<count;i+=2){int16_t sample=int16_t((i/2)%6000-3000);pcm[i]=uint8_t(sample);pcm[i+1]=uint8_t(uint16_t(sample)>>8);}
  {std::ofstream f(path,std::ios::binary);f.write((const char*)header.data(),44);f.write((const char*)pcm.data(),count);}
  assert(validate_loop_file(path.string()));
  std::string error; FILE *file=open_loop(path.string(),error);assert(file);
  set_gain(1000);
  // Header probing, unaligned seeks and repeated loop boundaries are decoder-visible behavior.
  std::vector<uint8_t> out(count*2+137);
  assert(fseek(file,44,SEEK_SET)==0);
  for(size_t offset=0;offset<out.size();) {
    size_t n=std::min<size_t>(137,out.size()-offset);
    assert(fread(out.data()+offset,1,n,file)==n);offset+=n;
  }
  for(size_t i=0;i<out.size();++i) assert(out[i]==pcm[i%count]);
  set_gain(0);assert(fseek(file,45,SEEK_SET)==0);
  assert(fread(out.data(),1,137,file)==137);
  for(unsigned i=0;i<137;++i)assert(out[i]==0);
  assert(fseek(file,-1,SEEK_SET)!=0);fclose(file);
  {std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<"not a wav";}
  assert(!validate_loop_file(path.string()) && !open_loop(path.string(),error));
  std::filesystem::remove(path);
  std::cout<<"Ambient: timer deadlines/wrap, menu lifetime, source validation, arbitrary reads, continuous loops and source-only fade PASS\n";
}
