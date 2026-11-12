"""Inject stdio read errors through the product decoder boundary and IDLE callback."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
source = root / 'main/smart_speaker/smart_speaker_audio_read_guard.cpp'
assert source.exists(), 'Player currently drops the stdio read-error signal before IDLE'
runtime = (root / 'main/smart_speaker/smart_speaker_runtime.cpp').read_text()
idle = runtime.split('  case AUDIO_PLAYER_CALLBACK_EVENT_IDLE: {')[1].split('  case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE:')[0]
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    (p/'mp3dec.h').write_text('typedef void *HMP3Decoder;\n')
    (p/'test.cpp').write_text(r'''
#include "audio_mp3.h"
#include "smart_speaker_audio_read_guard.h"
#include <cassert>
#include <cerrno>
#include <mutex>
#include <string>
namespace ambient {bool take_read_error(){return false;}}
int drains=0;
namespace smart_speaker { bool dual_i2s_audio_drain(uint32_t timeout){assert(timeout==1500);++drains;return true;} }
DECODE_STATUS speaker_decode_mp3_unchecked(HMP3Decoder, FILE *fp, decode_data*, mp3_instance*) {
 char bytes[16]; fread(bytes,1,sizeof(bytes),fp);return DECODE_STATUS_DONE;
}
#ifdef __APPLE__
int fail_read(void*,char*,int) {errno=EIO;return -1;}
FILE *bad_file() {return funopen(nullptr,fail_read,nullptr,nullptr,nullptr);}
#else
ssize_t fail_read(void*,char*,size_t) {errno=EIO;return -1;}
FILE *bad_file() {cookie_io_functions_t io{};io.read=fail_read;return fopencookie(nullptr,"r",io);}
#endif
struct {std::mutex mutex;bool playing=true,paused=false,paused_by_volume=false,auto_advance=true,advance_queued=false;} g_runtime;
int queued=0;std::string error;
void queue_track_completion(bool) {++queued;}
void set_error_locked(const char *s) {error=s;}
void idle() {do {
''' + idle + r'''
while(false);}
int main() {
 speaker_audio_read_error_reset();
 FILE *file=tmpfile();assert(file);decode_data out{};mp3_instance mp3{};
 assert(decode_mp3(nullptr,file,&out,&mp3)==DECODE_STATUS_DONE);fclose(file);
 idle();assert(queued==1 && error.empty() && drains==1);
 file=bad_file();assert(file);speaker_audio_read_error_reset();g_runtime.playing=true;
 assert(decode_mp3(nullptr,file,&out,&mp3)==DECODE_STATUS_ERROR);assert(ferror(file));fclose(file);assert(drains==1);
 idle();assert(queued==1 && !g_runtime.playing && !g_runtime.auto_advance);
 assert(error=="track_read_failed");assert(!speaker_audio_read_error_take());
 speaker_audio_read_error_reset();assert(!speaker_audio_read_error_take());
}
''')
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(p),'-I'+str(source.parent),'-I'+str(root/'managed_components/chmorgan__esp-audio-player'),str(source),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Player read guard: real stdio error stops without auto-next; normal EOF advances')
