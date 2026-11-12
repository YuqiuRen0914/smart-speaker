#include "smart_speaker_ambient.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <new>

namespace smart_speaker::ambient {
namespace {
std::atomic<unsigned> gain{1000};
std::atomic<bool> read_error{false};
constexpr uint32_t virtual_bytes = 0x7fff0000;
uint32_t le(const uint8_t *p, int n) { uint32_t v=0; for (int i=0;i<n;++i) v |= uint32_t(p[i])<<(8*i); return v; }
void put(uint8_t *p, uint32_t v) { for (int i=0;i<4;++i) p[i]=uint8_t(v>>(8*i)); }
struct Loop {
  FILE *source = nullptr;
  std::array<uint8_t,44> header{};
  std::array<uint8_t,1026> buffer{};
  uint32_t bytes = 0, position = 0;
  ~Loop() { if (source) fclose(source); }
  bool load(const std::string &path) {
    source = fopen(path.c_str(),"rb");
    // A power loss between the two FAT renames retains the previous asset.
    if (!source) source = fopen((path+".bak").c_str(),"rb");
    if (!source || fread(header.data(),1,44,source)!=44) return false;
    auto *h=header.data();
    bytes=le(h+40,4);
    if (memcmp(h,"RIFF",4) || memcmp(h+8,"WAVEfmt ",8) ||
        le(h+16,4)!=16 || le(h+20,2)!=1 || le(h+22,2)!=2 ||
        le(h+24,4)!=22050 || le(h+28,4)!=88200 || le(h+32,2)!=4 ||
        le(h+34,2)!=16 || memcmp(h+36,"data",4) ||
        bytes<88200 || bytes>64*1024*1024 || bytes%4 ||
        le(h+4,4)!=bytes+36 || fseek(source,0,SEEK_END) ||
        ftell(source)!=long(bytes+44)) return false;
    put(h+4,virtual_bytes+36); put(h+40,virtual_bytes);
    return true;
  }
};
int read_loop(void *cookie, char *out, int requested) {
  auto &s=*static_cast<Loop*>(cookie);
  int done=0;
  const unsigned level=gain.load(std::memory_order_relaxed);
  while (done<requested && s.position<virtual_bytes+44) {
    if (s.position<44) { out[done++]=char(s.header[s.position++]); continue; }
    uint32_t offset=(s.position-44)%s.bytes;
    const unsigned skip=offset&1U;
    offset-=skip;
    unsigned count=std::min<unsigned>({1024U,unsigned(requested-done),virtual_bytes+44-s.position,s.bytes-offset-skip});
    unsigned read=(count+skip+1)&~1U;
    if (fseek(s.source,long(44+offset),SEEK_SET) || fread(s.buffer.data(),1,read,s.source)!=read) {
      read_error.store(true); errno=EIO; return done ? done : -1;
    }
    for (unsigned i=0;i<read;i+=2) {
      int16_t sample=int16_t(uint16_t(s.buffer[i])|(uint16_t(s.buffer[i+1])<<8));
      uint16_t scaled=uint16_t(int(sample)*int(level)/1000);
      s.buffer[i]=uint8_t(scaled); s.buffer[i+1]=uint8_t(scaled>>8);
    }
    memcpy(out+done,s.buffer.data()+skip,count);
    s.position+=count; done+=int(count);
  }
  return done;
}
#if defined(ESP_PLATFORM) || !defined(__APPLE__)
using Offset=off_t;
#else
using Offset=fpos_t;
#endif
Offset seek_loop(void *cookie, Offset offset, int origin) {
  auto &s=*static_cast<Loop*>(cookie);
  if (origin!=SEEK_SET && origin!=SEEK_CUR && origin!=SEEK_END) { errno=EINVAL; return -1; }
  int64_t base=origin==SEEK_SET ? 0 : origin==SEEK_CUR ? s.position : virtual_bytes+44;
  if (int64_t(offset)<-base || int64_t(offset)>int64_t(virtual_bytes+44)-base) { errno=EINVAL; return -1; }
  s.position=uint32_t(base+offset); return Offset(s.position);
}
int close_loop(void *cookie) { delete static_cast<Loop*>(cookie); return 0; }
}
void set_gain(unsigned value) { gain.store(std::min(1000U,value),std::memory_order_relaxed); }
bool take_read_error() { return read_error.exchange(false); }
bool validate_loop_file(const std::string &path) { Loop loop; return loop.load(path); }
FILE *open_loop(const std::string &path, std::string &error) {
  auto *loop=new(std::nothrow) Loop;
  if (!loop) { error="ambient_memory_unavailable"; return nullptr; }
  if (!loop->load(path)) { delete loop; error="ambient_source_missing"; return nullptr; }
#if defined(ESP_PLATFORM) || defined(__APPLE__)
  FILE *file=funopen(loop,read_loop,nullptr,seek_loop,close_loop);
#else
  cookie_io_functions_t io{};
  io.read=[](void *cookie,char *buffer,size_t size)->ssize_t {
    return read_loop(cookie,buffer,int(std::min<size_t>(size,0x7fffffff)));
  };
  io.seek=[](void *cookie,off64_t *offset,int origin)->int {
    auto result=seek_loop(cookie,*offset,origin);
    if(result<0) return -1; *offset=result; return 0;
  };
  io.close=close_loop;
  FILE *file=fopencookie(loop,"r",io);
#endif
  if (!file) { delete loop; error="ambient_memory_unavailable"; }
  else read_error.store(false);
  return file;
}
}
