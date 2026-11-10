#include "runtime_internal.h"
#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include <algorithm>
#include <cassert>
#include <cstring>
using namespace smart_speaker;
using namespace smart_speaker::detail;
namespace smart_speaker::detail { Runtime g_runtime; std::mutex g_player_operation_mutex; }
#include "lyrics_api.h"

std::string upload(const std::string &text,const char *path="song.mp3") {
  DeviceBulkRequest r; r.arg=[](const char*,void *context){return std::string(static_cast<const char *>(context));};r.context=const_cast<char *>(path); r.body_size=text.size(); r.body_data=reinterpret_cast<const uint8_t *>(text.data());
  JsonDocument response; return handle_lyrics_upload(r,response.to<JsonObject>()).error;
}
std::string offset(int value) {
  JsonDocument req,res; req["path"]="song.mp3"; req["delay_ms"]=value;
  return handle_lyrics_offset(req.as<JsonVariantConst>(),res.to<JsonObject>()).error;
}
struct Stream {std::string text; size_t at=0; bool fail=false;};
int read_chunk(uint8_t *out,size_t size,void *ctx) {
  auto &s=*static_cast<Stream*>(ctx); if(s.fail&&s.at>0)return -1;
  size=std::min({size,size_t(3),s.text.size()-s.at});
  memcpy(out,s.text.data()+s.at,size);s.at+=size;return int(size);
}
int main() {
 g_runtime.playlist={{"song","song.mp3"},{"other","other.mp3"}};g_runtime.current_index=0;
  assert(upload("[00:01]原创测试\n[00:02]\n").empty());
  assert(g_runtime.playlist[0].has_lyrics && !g_runtime.playlist[1].has_lyrics);
  assert(g_runtime.lyrics->line(1000)=="原创测试");
  assert(upload("bad") == "lyrics_timestamps_required");
  assert(lyrics::load("song.mp3")->line(1000)=="原创测试");
  assert(upload("[00:01]x","../escape.mp3")=="track_not_found");
  assert(offset(500).empty() && g_runtime.lyrics_delay_ms==500);
  assert(lyrics::delay("song.mp3")==500 && g_runtime.lyrics->line(1000,500).empty());
  assert(offset(10001)=="lyrics_offset_invalid");
  Stream stream{"[00:01]streamed\n"}; DeviceBulkRequest r;
  r.content_length=stream.text.size(); r.read_chunk=read_chunk; r.context=&stream; r.arg=[](const char*,void*){return std::string("song.mp3");};
  JsonDocument res;
  assert(handle_lyrics_upload(r,res.to<JsonObject>()).error.empty());
  assert(g_runtime.lyrics->line(1000)=="streamed");
  stream.at=0;stream.fail=true;
  assert(handle_lyrics_upload(r,res.to<JsonObject>()).error=="lyrics_read_failed");
  assert(lyrics::load("song.mp3")->line(1000)=="streamed");
  JsonDocument remove_request,remove_response; remove_request["path"]="song.mp3";
  assert(handle_lyrics_remove(remove_request.as<JsonVariantConst>(),remove_response.to<JsonObject>()).error.empty());
  assert(!lyrics::load("song.mp3") && !g_runtime.lyrics && !g_runtime.playlist[0].has_lyrics);
  r.content_length=lyrics::kMaxBytes+1;
  assert(handle_lyrics_upload(r,res.to<JsonObject>()).error=="lyrics_size_invalid");
}
