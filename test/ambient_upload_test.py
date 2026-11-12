"""Exercise the actual upload handler with interrupted streams and a real temp SD."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/smart_speaker/smart_speaker_projection.cpp').read_text()
start = source.index('DeviceOperationResult handle_ambient_upload(const DeviceBulkRequest &request')
handler = source[start:source.index('DeviceOperationResult fill_wallpapers(JsonObject response)', start)]
fixture = r'''
#include "smart_speaker_ambient.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <sys/stat.h>
namespace ambient=smart_speaker::ambient;
struct DeviceOperationResult { bool ok; std::string error; };
DeviceOperationResult device_operation_error(const char *s,int){return {false,s};}
DeviceOperationResult device_operation_ack(){return {true,""};}
struct DeviceBulkRequest {
  const uint8_t *body_data=nullptr; size_t body_size=0,content_length=0;
  int (*read_chunk)(void*,size_t,void*)=nullptr; void *context=nullptr;
  std::string scene="shore";
};
std::string request_arg(const DeviceBulkRequest &r,const char*) {return r.scene;}
struct {std::mutex mutex;bool playing=false,paused=false,uploading=false,assistant_focus_active=false,game_focus_active=false;} g_runtime;
std::mutex g_player_operation_mutex;
constexpr int AUDIO_PLAYER_STATE_IDLE=0;
int player_state=0;int audio_player_get_state(){return player_state;}
std::string sd;bool mounted=true;
namespace lunaredu::device {
bool tfcard_resolve_path(const char *path,std::string &out) {out=sd+path;return mounted;}
}
''' + handler + r'''
int main(int argc,char **argv) {
 assert(argc==2);sd=argv[1];std::filesystem::create_directories(sd+"/ambient");
 std::vector<uint8_t> wav(88244,0);
 auto put=[&](int at,unsigned v,int bytes=4){for(int i=0;i<bytes;++i)wav[at+i]=uint8_t(v>>(i*8));};
 memcpy(wav.data(),"RIFF",4);put(4,88236);memcpy(wav.data()+8,"WAVEfmt ",8);
 put(16,16);put(20,1,2);put(22,2,2);put(24,22050);put(28,88200);put(32,4,2);put(34,16,2);
 memcpy(wav.data()+36,"data",4);put(40,88200);wav[44]=42;
 DeviceBulkRequest r;r.body_data=wav.data();r.body_size=wav.size();
 JsonDocument response;
 auto run=[&](){response.clear();return handle_ambient_upload(r,response.to<JsonObject>());};
 assert(run().ok && !g_runtime.uploading);
 auto read=[&](){std::ifstream f(sd+"/ambient/shore.wav",std::ios::binary);return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});};
 assert(read()==wav);
 auto original=wav;wav[0]='?';assert(!run().ok && read()==original && !g_runtime.uploading);wav=original;
 r.body_data=nullptr;r.body_size=0;r.content_length=wav.size();
 int chunks=0;r.context=&chunks;
 r.read_chunk=[](void *dest,size_t size,void *ctx)->int {auto &n=*static_cast<int*>(ctx);if(n++)return -1;memset(dest,0,size);return int(size);};
 assert(!run().ok && read()==original && !g_runtime.uploading);
 assert(!std::filesystem::exists(sd+"/ambient/shore.wav.part"));
 r.body_data=wav.data();r.body_size=wav.size();r.content_length=0;
 g_runtime.paused=true;assert(!run().ok && read()==original);g_runtime.paused=false;
 player_state=1;assert(!run().ok);player_state=0;
 r.scene="../shore";assert(!run().ok);r.scene="shore";
 mounted=false;assert(!run().ok && !g_runtime.uploading);mounted=true;
 // Recovery of the prior publication after a reset in the FAT rename window.
 std::filesystem::rename(sd+"/ambient/shore.wav",sd+"/ambient/shore.wav.bak");
 wav[44]=12;assert(run().ok && read()==wav);
 assert(!std::filesystem::exists(sd+"/ambient/shore.wav.bak"));
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-ambient-upload-') as tmp:
    tmp = Path(tmp)
    (tmp/'test.cpp').write_text(fixture)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
        '-I'+str(root/'main/smart_speaker'),
        '-I'+str(root/'managed_components/bblanchon__arduinojson/src'),
        str(tmp/'test.cpp'), str(root/'main/smart_speaker/smart_speaker_ambient.cpp'),
        '-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test'),str(tmp/'sd')],check=True)
print('Ambient upload: canonical validation, interruption rollback, player ownership, path rejection and reset recovery PASS')
