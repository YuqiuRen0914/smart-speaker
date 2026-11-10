#include "transfer_internal.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace smart_speaker;
using namespace smart_speaker::detail;
static uint32_t clock_ms;
uint32_t smart_speaker::detail::now_ms() { return clock_ms; }
struct Reader { std::vector<uint8_t> bytes; size_t offset=0; size_t fail_after=SIZE_MAX; };
static int read_chunk(uint8_t *out,size_t size,void *context) {
 auto &r=*static_cast<Reader *>(context);
 if(r.offset>=r.fail_after) return -1;
 size=std::min({size,size_t(8),r.bytes.size()-r.offset});
 memcpy(out,r.bytes.data()+r.offset,size);r.offset+=size;clock_ms+=5;return int(size);
}
int main() {
 assert(!dismiss_transfer());g_transfer.generation=1;g_transfer.receiving=true;
 assert(dismiss_transfer());assert(transfer_dismissed(1));assert(!dismiss_transfer());
 g_transfer.generation=2;g_transfer.receiving=false;g_transfer.batch_open=true;g_transfer.updated_ms=0xfffffff0U;clock_ms=30;
 assert(transfer_snapshot().batch_open);clock_ms=16000;assert(!transfer_snapshot().batch_open);assert(dismiss_transfer());
 ++g_transfer.generation;clock_ms+=3001;assert(!dismiss_transfer());
 assert(safe_upload_name("Song.MP3"));assert(!safe_upload_name("../song.mp3"));assert(!safe_upload_name("x/y.mp3"));assert(!safe_upload_name("x.wav"));
 Reader r; r.bytes.resize(300,1);r.bytes[0]=0xff;r.bytes[1]=0xfb;r.bytes[2]=0x90;r.bytes[3]=0;
 DeviceBulkRequest req;req.content_length=r.bytes.size();req.read_chunk=read_chunk;req.context=&r;
 size_t written=0;g_transfer.received=0;
 assert(stream_upload_to_file(req,"stream.tmp",written));assert(written==300&&g_transfer.received==300);
 FILE *file=fopen("stream.tmp","rb");std::vector<uint8_t> actual(300);assert(fread(actual.data(),1,300,file)==300);fclose(file);assert(actual==r.bytes);
 r.offset=0;r.fail_after=16;written=0;g_transfer.received=0;
 assert(!stream_upload_to_file(req,"stream.tmp",written));assert(g_transfer.received==16);
 // An ID3 prefix and a trailing ID3v1 tag are stripped by the real normalizer.
 std::vector<uint8_t> tagged(10,0);memcpy(tagged.data(),"ID3",3);tagged[3]=3;
 tagged.insert(tagged.end(),r.bytes.begin(),r.bytes.end());size_t tail=tagged.size();tagged.resize(tail+128,0);memcpy(tagged.data()+tail,"TAG",3);
 req={};req.body_data=tagged.data();req.body_size=tagged.size();written=0;
 assert(stream_upload_to_file(req,"stream.tmp",written));assert(written==300&&g_transfer.received==tagged.size());
 std::vector<uint8_t> invalid(100,0);req.body_data=invalid.data();req.body_size=invalid.size();written=0;
 assert(!stream_upload_to_file(req,"stream.tmp",written));
 std::remove("stream.tmp");
}
