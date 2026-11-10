"""Run the production checked player exit against asynchronous decoder states."""
from pathlib import Path
import os
import subprocess,tempfile
app=Path(__file__).resolve().parents[1]
repo=Path(__file__).resolve().parents[1]
source=(app/'main/smart_speaker/smart_speaker_runtime.cpp').read_text(encoding='utf-8')
start=source.index('bool stop_music_checked(')
body=source[start:source.index('\n}',start)+2]
locked_start=source.index('bool detail::stop_music_locked(')
locked=source[locked_start:source.index('\n}',locked_start)+2].replace('bool detail::stop_music_locked','bool stop_music_locked')
service_start=source.index('bool stop_audio_service(')
service_body=source[service_start:source.index('\n}',service_start)+2]
callbacks=source[source.index('esp_err_t i2s_write_scaled('):source.index('void handle_audio_event(FilePlayerEventContext *);')]
code=r'''
#include "lunaredu_music_playback_coordinator.h"
#include <atomic>
#include <cassert>
#include <mutex>
#include <string>
constexpr int ESP_OK=0,pdTRUE=1;
using esp_err_t=int;using i2s_slot_mode_t=int;
enum class AudioMute{Mute,Unmute};enum class FilePlayerState{Idle,Playing,Paused};
#define ESP_LOGI(...) ((void)0)

std::mutex g_player_operation_mutex;
std::atomic<bool> g_library_refresh_pending{false};
std::atomic<bool> g_player_retiring{false};
std::atomic<bool> g_ota_maintenance{false};
struct Runtime:lunaredu::music::PlaybackState {std::mutex mutex;Runtime(){audio_ready=true;advance_queued=true;}} g_runtime;
struct Command{};
int queued=4,g_command_queue=1,stops=0;
FilePlayerState state=FilePlayerState::Paused;
bool g_file_player_created=true,muted=true;
int writes=0,reconfigurations=0,unmutes=0;
esp_err_t dual_i2s_audio_write(const void *,size_t n,size_t *written,uint32_t,void *){++writes;*written=n;return ESP_OK;}
esp_err_t dual_i2s_audio_reconfigure(uint32_t,uint32_t,unsigned){++reconfigurations;return ESP_OK;}
void dual_i2s_track_clock_reset(){}
struct Status {bool playing=false,paused=true;} observed;
int xQueueReceive(int,Command *,int){if(queued){--queued;return pdTRUE;}return 0;}
FilePlayerState file_player_state(){return state;}

int file_player_stop(){++stops;return ESP_OK;}
int dual_i2s_audio_mute(AudioMute value){if(value==AudioMute::Unmute)++unmutes;return ESP_OK;}
bool dual_i2s_audio_fade_to_mute(int){return muted;}
Status status_snapshot(){return observed;}
void update_play_state(bool playing,bool paused,bool automatic){observed={playing,paused};g_runtime.auto_advance=automatic;}
bool file_retired=false,output_retired=false,provider_pinned=true;
int output_stops=0;
namespace lunaredu::lunaros {struct KernelRuntime {};}
bool stop_file_player(std::string &e){if(!file_retired){e="decoder_pending";return false;}e.clear();return true;}
bool stop_dual_i2s_audio(lunaredu::lunaros::KernelRuntime &){++output_stops;return output_retired;}
namespace lunaredu::audio {
struct Broker {bool stop(bool(*backend)(void *,std::string &),void *p,std::string &e){if(!backend(p,e))return false;provider_pinned=false;return true;}};
Broker &local_audio_focus(){static Broker broker;return broker;}
}
lunaredu::music::PlaybackCoordinator g_playback_coordinator{g_runtime,g_runtime.mutex,{},[]{
 lunaredu::music::PlaybackPorts p;
 p.idle=[](void *){return !g_file_player_created||file_player_state()==FilePlayerState::Idle;};
 p.stop=[](void *){return file_player_stop()==ESP_OK;};
 p.discard_pending=[](void *){Command c;while(xQueueReceive(g_command_queue,&c,0)==pdTRUE){}};
 p.fade_to_mute=[](void *,uint32_t ms){return dual_i2s_audio_fade_to_mute(ms);};return p;
}()};
'''+callbacks+service_body+locked+body.replace('detail::stop_music_locked','stop_music_locked')+r'''
int main(){
 std::string error;
 size_t written=0;int16_t pcm=0;
 assert(i2s_write_scaled(&pcm,2,&written,0,nullptr)==ESP_OK&&writes==1&&written==2);
 for(auto *gate:{&g_player_retiring,&g_ota_maintenance}){
   *gate=true;
   assert(i2s_write_scaled(&pcm,2,&written,0,nullptr)==ESP_OK&&writes==1);
   assert(i2s_reconfig_clock(44100,16,2)==ESP_OK&&reconfigurations==0);
   assert(audio_mute(AudioMute::Unmute)==ESP_OK&&unmutes==0);
   *gate=false;
 }
 assert(!stop_music_checked(error)&&stops==1&&queued==0);
 assert(g_player_retiring&&!g_runtime.auto_advance&&!g_runtime.advance_queued);
 assert(!stop_music_checked(error)&&stops==1);
 state=FilePlayerState::Idle;muted=false;
 assert(!stop_music_checked(error)&&g_player_retiring);
 muted=true;assert(stop_music_checked(error)&&!g_player_retiring);
 assert(stop_music_checked(error)&&stops==1);
 state=FilePlayerState::Playing;observed={true,false};
 assert(!stop_music_checked(error)&&stops==2);
 state=FilePlayerState::Idle;observed={false,false};assert(stop_music_checked(error));
 lunaredu::lunaros::KernelRuntime runtime;
 assert(!stop_audio_service(runtime,error)&&g_file_player_created&&provider_pinned&&output_stops==0);
 file_retired=true;
 assert(!stop_audio_service(runtime,error)&&!g_file_player_created&&provider_pinned&&output_stops==1);
 output_retired=true;
 assert(stop_audio_service(runtime,error)&&!provider_pinned&&output_stops==2);
 assert(stop_audio_service(runtime,error));
 g_file_player_created=true;provider_pinned=true;
 assert(stop_audio_service(runtime,error)&&!provider_pinned);
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-retirement-') as tmp:
    path=Path(tmp)/'test.cpp';binary=Path(tmp)/'test.exe';path.write_text(code,encoding='utf-8')
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I'+str(repo/'lunaros/apps/optional/app_music/include'),str(repo/'lunaros/apps/optional/app_music/src/playback_coordinator.cpp'),str(repo/'lunaros/apps/optional/app_music/src/playback_mode.cpp'),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Shared player retirement: paused stop, one STOP, mute acknowledgement, retry, later session and provider unbinding after physical retirement passed')
