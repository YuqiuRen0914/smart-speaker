"""Exercise the production upload transaction and incremental library publish."""
from pathlib import Path
import os, subprocess, tempfile
app=Path(__file__).resolve().parents[1]
repo=Path(__file__).resolve().parents[1]
projection=(app/'main/smart_speaker/smart_speaker_projection.cpp').read_text(encoding='utf-8')
runtime=(app/'main/smart_speaker/smart_speaker_runtime.cpp').read_text(encoding='utf-8')
start=projection.index('DeviceOperationResult handle_file_upload(')
handler=projection[start:projection.index('\n}',start)+2]
start=runtime.index('void publish_imported_track(')
publish=runtime[start:runtime.index('\n}',start)+2]
code=r'''
#include "runtime_internal.h"
#include "transfer_internal.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#include <thread>
namespace lunaredu::lunaros {struct DeviceApiContext {};}
namespace lunaredu::device {bool card_ready=true;bool tfcard_store_ready(){return card_ready;}}
namespace smart_speaker::detail {
Runtime g_runtime;
std::mutex g_player_operation_mutex;
uint32_t clock_ms=0;
int stops=0,stop_pending=0;
uint32_t now_ms(){return clock_ms;}
bool stop_music_locked(std::string &error){++stops;if(stop_pending-- > 0){error="music_stop_pending";return false;}error.clear();return true;}
void set_error_locked(const char *error){g_runtime.last_error=error;}
void clear_error_locked(){g_runtime.last_error.clear();}
bool ensure_music_dir(){return std::filesystem::exists("music")||std::filesystem::create_directory("music");}
'''+publish+r'''
}
void vTaskDelay(int ms){smart_speaker::detail::clock_ms+=ms;}
namespace smart_speaker {
using namespace lunaredu::lunaros;
using namespace detail;
constexpr const char *kMusicDir="music";
std::string request_arg(const DeviceBulkRequest &r,const char *name){return r.arg?r.arg(name,r.context):std::string();}
RuntimeStatus status_snapshot(){return {};}
void fill_status(JsonObject out,const RuntimeStatus &){out["playlist_revision"]=g_runtime.playlist_revision;}
'''+handler+r'''
}
struct Input {std::string name;std::vector<uint8_t> bytes;size_t offset=0,fail_after=SIZE_MAX;};
std::vector<uint8_t> mp3(){std::vector<uint8_t> bytes(417,1);bytes[0]=0xff;bytes[1]=0xfb;bytes[2]=0x90;bytes[3]=0;return bytes;}
int main(){
 using namespace smart_speaker;using namespace smart_speaker::detail;using namespace lunaredu::lunaros;
 DeviceApiContext device;JsonDocument response;
 Input input{"b.mp3",mp3()};
 DeviceBulkRequest request;request.context=&input;
 request.arg=[](const char *,void *p){return static_cast<Input *>(p)->name;};
 request.read_chunk=[](uint8_t *p,size_t n,void *context){auto &in=*static_cast<Input *>(context);if(in.offset>=in.fail_after)return -1;n=std::min({n,size_t(8),in.bytes.size()-in.offset});std::memcpy(p,in.bytes.data()+in.offset,n);in.offset+=n;return int(n);};
 const auto invoke=[&]{response.clear();input.offset=0;request.content_length=input.bytes.size();return handle_file_upload(device,request,response.to<JsonObject>());};
 stop_pending=2;auto result=invoke();assert(result.ok&&stops==3);
 assert(std::filesystem::file_size("music/b.mp3")==417&&!std::filesystem::exists("music/b.mp3.upload"));
 assert(g_runtime.playlist_revision==1&&g_runtime.playlist.size()==1&&g_runtime.current_index==0&&!g_runtime.transfer_active&&!transfer_snapshot().receiving);
 assert(transfer_snapshot().received==417&&response["size"].as<size_t>()==417);
 input.name="a.mp3";assert(invoke().ok);
 assert(g_runtime.playlist_revision==2&&g_runtime.playlist.size()==2&&g_runtime.current_index==1&&g_runtime.playlist[1].name=="b.mp3");
 int previous_stops=stops;result=invoke();assert(!result.ok&&result.status_code==409&&stops==previous_stops);
 input.name="bad.mp3";input.bytes[1]=0xff;result=invoke();assert(!result.ok&&result.status_code==400&&result.error=="audio_mp3_invalid");
 assert(!std::filesystem::exists("music/bad.mp3")&&!std::filesystem::exists("music/bad.mp3.upload")&&g_runtime.playlist_revision==2);
 input.bytes=mp3();input.fail_after=16;result=invoke();assert(!result.ok&&result.status_code==400&&!std::filesystem::exists("music/bad.mp3.upload"));
 input.fail_after=SIZE_MAX;input.name="../bad.mp3";result=invoke();assert(!result.ok&&result.status_code==400);
 input.name="c.mp3";lunaredu::device::card_ready=false;result=invoke();assert(!result.ok&&result.status_code==503);lunaredu::device::card_ready=true;
 stop_pending=1000;result=invoke();assert(!result.ok&&result.status_code==503&&!g_runtime.transfer_active&&!transfer_snapshot().receiving&&!std::filesystem::exists("music/c.mp3"));
 stop_pending=0;input.bytes.resize(32U*1024U*1024U+1);result=invoke();assert(!result.ok&&result.status_code==413);input.bytes=mp3();
 std::atomic<bool> held{false},release{false};std::thread other([&]{std::lock_guard<std::mutex> lock(g_player_operation_mutex);held=true;while(!release.load())std::this_thread::yield();});while(!held.load())std::this_thread::yield();
 result=invoke();assert(!result.ok&&result.status_code==409);release=true;other.join();
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-upload-api-') as tmp:
    path=Path(tmp);(path/'test.cpp').write_text(code,encoding='utf-8')
    includes=[app/'test/host',app/'main/smart_speaker',repo/'lunaros/services/optional/service_audio/include',repo/'lunaros/apps/optional/app_music/include',repo/'lunaros/os/core/include',repo/'lunaros/os/device_api/include',app/'managed_components/bblanchon__arduinojson/src']
    sources=[path/'test.cpp',app/'main/smart_speaker/smart_speaker_transfer.cpp',repo/'lunaros/services/optional/service_audio/src/audio_import.cpp',repo/'lunaros/apps/optional/app_music/src/lyrics.cpp',repo/'lunaros/os/device_api/src/projections/device_projection_json.cpp']
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-Wall','-Wextra','-Werror','-pthread',*['-I'+str(p) for p in includes],*[str(p) for p in sources],'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],cwd=tmp,check=True)
print('PASS production upload transaction: checked-stop retry/timeout, invalid names/format, size/card/busy guards, cancellation cleanup, no overwrite and incremental revision/selection preservation')
