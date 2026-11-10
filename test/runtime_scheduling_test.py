"""Run the real main-loop dequeue logic under player contention and command bursts."""
from pathlib import Path
import os
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
source = (app / 'main/smart_speaker/smart_speaker_runtime.cpp').read_text(encoding='utf-8')
tick = source[source.index('void poll_library_refresh(){'):source.index('\nnamespace {\nRuntimeStatus status_locked()')]
code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#define ESP_LOGW(...) ((void)0)
constexpr int pdTRUE=1;
struct Runtime {
 std::mutex mutex;bool playing=false,paused=false;
 std::atomic<bool> first_frame_seen{false},decode_timeout_skip_sent{false};
 std::deque<int> playlist;uint32_t play_started_ms=0;
} g_runtime;
std::atomic<bool> g_player_retiring{false},g_ota_maintenance{false},g_library_refresh_pending{false};
int scans=0;void rescan_library(){++scans;}
std::mutex g_player_operation_mutex;
struct Command {int value=0;};
std::deque<Command> queue;
void *g_command_queue=reinterpret_cast<void *>(1);
int processed=0,housekeeping=0;
bool stop_music_checked(std::string &){return true;}
void tick_smart_speaker_sensors(){++housekeeping;}
void poll_encoder(){}void dual_i2s_audio_tick(){}void capture_dual_audio_debug_if_due(){}
void process_pending_volume(){}
uint32_t now_ms(){return 0;}
struct Coordinator {void tick(){}} g_playback_coordinator;
int xQueueReceive(void *,Command *out,int){if(queue.empty())return 0;*out=queue.front();queue.pop_front();return pdTRUE;}
void process_command(const Command &command){assert(command.value==++processed);}
''' + tick + r'''
int main(){
 for(int i=1;i<=12;++i)queue.push_back({i});
 std::atomic<bool> locked{false},release{false};
 std::thread transfer([&]{std::lock_guard<std::mutex> hold(g_player_operation_mutex);locked=true;
   while(!release.load())std::this_thread::yield();});
 while(!locked.load())std::this_thread::yield();
 g_library_refresh_pending=true;
 for(int i=0;i<100;++i){tick_runtime();poll_library_refresh();}
 assert(scans==0&&g_library_refresh_pending);
 assert(queue.size()==12&&processed==0&&housekeeping==100);
 release=true;transfer.join();
 poll_library_refresh();poll_library_refresh();assert(scans==1&&!g_library_refresh_pending);
 g_library_refresh_pending=true;g_ota_maintenance=true;poll_library_refresh();
 assert(scans==1&&!g_library_refresh_pending);g_ota_maintenance=false;
 for(int i=1;i<=12;++i){tick_runtime();assert(processed==i&&queue.size()==size_t(12-i));}
 tick_runtime();assert(processed==12);
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-scheduling-') as tmp:
    path = Path(tmp)
    (path / 'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra',
                    '-Werror', '-pthread', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Main scheduling: transfer contention preserves FIFO; one command per tick; housekeeping remains live')
