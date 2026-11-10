#include "transfer_internal.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace smart_speaker;
using namespace smart_speaker::detail;
struct Reader { std::vector<uint8_t> bytes; size_t offset=0;size_t fail_after=SIZE_MAX; };
static int read_chunk(uint8_t *out,size_t n,void *context) {
 auto &r=*static_cast<Reader *>(context);if(r.offset>=r.fail_after)return -1;
 n=std::min({n,size_t(8),r.bytes.size()-r.offset});memcpy(out,r.bytes.data()+r.offset,n);r.offset+=n;return int(n);
}
int main() {
 assert(safe_upload_name("Song.MP3"));
 for(const char *name:{"../song.mp3","x/y.mp3","x.wav","x\\y.mp3","a:bad.mp3","a?.mp3","x\x01.mp3"})assert(!safe_upload_name(name));
 Reader r;r.bytes.resize(42,0);memcpy(r.bytes.data(),"ID3",3);r.bytes[3]=3;r.bytes[9]=32;
 r.bytes[12]=0xff;r.bytes[13]=0xfb;r.bytes[14]=0x90;
 auto at=r.bytes.size();r.bytes.resize(at+417,1);r.bytes[at]=0xff;r.bytes[at+1]=0xfb;r.bytes[at+2]=0x90;r.bytes[at+3]=0;
 DeviceBulkRequest req;req.content_length=r.bytes.size();req.read_chunk=read_chunk;req.context=&r;
 size_t written=0;std::string error;
 assert(stream_upload_to_file(req,"stream.tmp",written,error));assert(written==r.bytes.size()&&transfer_snapshot().received==written);
 FILE *file=fopen("stream.tmp","rb");std::vector<uint8_t> actual(written);assert(fread(actual.data(),1,written,file)==written);fclose(file);assert(actual==r.bytes);
 req={};req.body_data=r.bytes.data();req.body_size=r.bytes.size();
 assert(stream_upload_to_file(req,"stream.tmp",written,error)&&written==r.bytes.size());
 r.offset=0;r.fail_after=16;req={};req.content_length=r.bytes.size();req.read_chunk=read_chunk;req.context=&r;
 assert(!stream_upload_to_file(req,"stream.tmp",written,error));assert(transfer_snapshot().received==16);assert(!fopen("stream.tmp","rb"));
}
