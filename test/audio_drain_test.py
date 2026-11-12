"""Exercise the production EOF drain against bounded queue/tick doubles."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'main/smart_speaker/smart_speaker_audio.cpp').read_text()
body=s[s.index('bool dual_i2s_audio_drain('):s.index('esp_err_t dual_i2s_audio_mute(')]
code=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
using TickType_t=uint32_t;
#define pdMS_TO_TICKS(x) (x)
constexpr unsigned kBlockCount=64,kDmaDescriptorCount=4,kFramesPerBlock=128;
void *g_free_queue=reinterpret_cast<void*>(1);unsigned free_blocks=61,ticks=0;bool stuck=false;
bool g_output_muted=false,g_audio_expected=true;
struct {bool fault_muted=false;uint32_t sample_rate=44100;}g_diagnostics;
unsigned uxQueueMessagesWaiting(void*){return free_blocks;}
unsigned now_ms(){return ticks;}
void vTaskDelay(unsigned n){ticks+=n;if(!stuck&&free_blocks<kBlockCount)++free_blocks;}
''' + body + r'''
int main(){
 assert(dual_i2s_audio_drain(1500));assert(free_blocks==64 && ticks==15 && !g_audio_expected);
 free_blocks=60;ticks=0;stuck=true;g_audio_expected=true;
 assert(!dual_i2s_audio_drain(20));assert(ticks==20 && g_audio_expected);
 g_output_muted=true;ticks=0;assert(!dual_i2s_audio_drain(1500));assert(ticks==0);
 g_output_muted=false;g_diagnostics.fault_muted=true;assert(!dual_i2s_audio_drain(1500));
 g_diagnostics.fault_muted=false;stuck=false;free_blocks=63;ticks=UINT32_MAX;g_diagnostics.sample_rate=8000;
 assert(dual_i2s_audio_drain(1500));assert(ticks==64 && !g_audio_expected);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('EOF drain: PCM and DMA tail, timeout, mute/fault and clock wrap passed')
