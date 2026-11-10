#include "runtime_internal.h"
#include "device_api/device_operation_types.h"
#include <algorithm>
#include <cassert>
#include <cstring>
namespace lunaredu::lunaros { struct DeviceApiContext {}; }
lunaredu::lunaros::DeviceApiContext g_device;
using namespace smart_speaker;
using namespace smart_speaker::detail;
namespace smart_speaker::detail { Runtime g_runtime; std::mutex g_player_operation_mutex; }
#include "smart_speaker_audio.h"
static bool ready=true,fail_remove=false,stop_timeout=false;
static int stopped=0,resumed=0,removed=0,played=-1,player_state=1;
static uint32_t clock_ms=0;
namespace lunaredu::device {bool tfcard_store_ready(){return ready;}}
void vTaskDelay(uint32_t ms){clock_ms+=ms;}
namespace lunaredu::audio {
esp_err_t file_player_resume(){++resumed;player_state=1;return 0;}
esp_err_t file_player_stop(){++stopped;if(!stop_timeout)player_state=0;return 0;}
FilePlayerState file_player_state(){return static_cast<FilePlayerState>(player_state);}
bool dual_i2s_audio_fade_to_mute(uint32_t){return true;}
}
extern "C" int __wrap_remove(const char*){++removed;return fail_remove?-1:0;}
namespace smart_speaker::detail {
uint32_t now_ms(){return clock_ms;}
void clear_error_locked(){g_runtime.last_error.clear();}
void set_error_locked(const char*e){g_runtime.last_error=e;}
bool play_index(int i){played=i;g_runtime.current_index=i;g_runtime.playing=true;return true;}
}
namespace smart_speaker {
lunaredu::lunaros::DeviceOperationResult fill_status_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonObject) { return {}; }
}

void reset(){g_runtime.playlist={{"a","/a"},{"b","/b"},{"c","/c"}};g_runtime.current_index=1;g_runtime.playing=true;g_runtime.paused=false;g_runtime.transfer_active=false;g_runtime.auto_advance=true;ready=true;fail_remove=stop_timeout=false;stopped=resumed=removed=0;played=-1;player_state=1;clock_ms=0;}
std::string erase(const char*p){JsonDocument req,res;req["path"]=p;return handle_track_remove_payload(g_device,req.as<JsonVariantConst>(),res.to<JsonObject>()).error;}
int main(){
reset();assert(erase("/a").empty());assert(stopped==0&&played==-1&&g_runtime.playing&&g_runtime.current_index==0&&g_runtime.playlist[0].path=="/b");
reset();assert(erase("/c").empty());assert(stopped==0&&g_runtime.current_index==1&&g_runtime.auto_advance);
reset();assert(erase("/b").empty());assert(stopped==1&&played==1&&g_runtime.playlist[1].path=="/c");
reset();g_runtime.current_index=2;assert(erase("/c").empty());assert(played==0);
reset();g_runtime.playlist.resize(1);g_runtime.current_index=0;assert(erase("/a").empty());assert(played==-1&&!g_runtime.playing&&g_runtime.current_index==-1);
reset();g_runtime.playing=false;g_runtime.paused=true;player_state=2;assert(erase("/b").empty());assert(resumed==0&&stopped==1&&played==-1&&!g_runtime.playing);
reset();g_runtime.paused_by_volume=true;assert(erase("/a").empty());assert(g_runtime.paused_by_volume);
reset();g_runtime.playing=false;g_runtime.paused=true;g_runtime.paused_by_volume=true;player_state=2;
assert(erase("/b").empty());assert(!g_runtime.paused_by_volume);
reset();fail_remove=true;assert(erase("/a")=="track_delete_failed");assert(stopped==0&&g_runtime.playlist.size()==3&&g_runtime.playing);
reset();fail_remove=true;assert(erase("/b")=="track_delete_failed");assert(played==1&&g_runtime.playlist.size()==3);
reset();stop_timeout=true;assert(erase("/b")=="playback_stop_timeout");assert(removed==0&&g_runtime.playlist.size()==3);
reset();assert(erase("/../secret")=="track_not_found");assert(removed==0&&stopped==0);
reset();g_runtime.transfer_active=true;assert(erase("/a")=="upload_in_progress");assert(removed==0);
reset();g_player_operation_mutex.lock();assert(erase("/a")=="speaker_busy");g_player_operation_mutex.unlock();
reset();JsonDocument req,res;req["path"]=42;assert(handle_track_remove_payload(g_device,req.as<JsonVariantConst>(),res.to<JsonObject>()).error=="invalid_track_path");
}
