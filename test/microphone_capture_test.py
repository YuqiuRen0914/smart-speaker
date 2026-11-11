"""Run the production microphone bridge against I2S doubles at music rates."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'main/smart_speaker/smart_speaker_audio.cpp').read_text()
enable = source[source.index('bool smart_speaker_voice_audio_enable_input('):source.index('bool smart_speaker_voice_audio_enable_output(')]
output = source[source.index('bool smart_speaker_voice_audio_enable_output('):source.index('bool smart_speaker_voice_audio_configure_output(')]
read = source[source.index('bool smart_speaker_voice_audio_read_input('):source.index('bool smart_speaker_voice_audio_write_output(')]
fixture = r'''
#include "smart_speaker_microphone_resampler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
using namespace smart_speaker;
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=1;
constexpr size_t kFramesPerBlock=128;
constexpr uint32_t kVoiceSampleRate=16000;
void *g_left_rx=reinterpret_cast<void *>(1);
void *g_reconfigure_done=reinterpret_cast<void *>(1);
bool g_reconfigure_pending=false;
uint32_t g_pending_sample_rate=44100;
std::atomic<bool> g_microphone_input_enabled{false},g_microphone_input_requested{false},g_voice_output_enabled{false};
std::atomic<uint32_t> g_microphone_clock_rate{44100},g_microphone_clock_generation{0},g_music_sample_rate{44100};
int32_t g_mic_raw[kFramesPerBlock*2]{};
struct {bool microphone_input_enabled=false,synchronized=true;}g_diagnostics;
uint32_t clock_ms=0;
uint32_t now_ms(){return clock_ms;}
int enabled=0,disabled=0,reads=0,mutes=0,fadeups=0,reconfigures=0;
bool right_slot=false,read_fault=false,change_during_read=false,clock_timeout=false;
size_t raw_frames_read=0;
int i2s_channel_enable(void *){++enabled;return ESP_OK;}
int i2s_channel_disable(void *){++disabled;return ESP_OK;}
void dual_i2s_audio_fade_to_mute(int){++mutes;}
void dual_i2s_audio_fade_up(){++fadeups;}
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
int xSemaphoreTake(void *,int){if(clock_timeout){clock_timeout=false;return 0;}++reconfigures;g_microphone_clock_rate=g_pending_sample_rate;g_reconfigure_pending=false;++g_microphone_clock_generation;return pdTRUE;}
int i2s_channel_read(void *,void *target,size_t bytes,size_t *received,uint32_t timeout) {
  assert(timeout>0);++reads;
  if(read_fault){*received=0;return ESP_ERR_INVALID_STATE;}
  auto *pcm=static_cast<int32_t *>(target);
  for(size_t i=0;i<bytes/8;++i){pcm[i*2]=right_slot?0:1000*65536;pcm[i*2+1]=right_slot?2000*65536:0;}
  *received=bytes;raw_frames_read+=bytes/8;
  if(change_during_read){++g_microphone_clock_generation;change_during_read=false;}
  return ESP_OK;
}
'''+enable+output+read+r'''
int main() {
  for(uint32_t rate:{8000U,16000U,22050U,32000U,44100U,48000U}) {
    MicrophoneResampler duration;duration.reset(rate);int16_t result[2]{};unsigned count=0;
    for(uint32_t input=0;input<rate;++input)count+=duration.push(1000,result);
    assert(count==16000); // Exact sample count over one second, no drift.
    for(bool right:{false,true}) {
      right_slot=right;raw_frames_read=0;g_microphone_clock_rate=rate;
      assert(smart_speaker_voice_audio_enable_input(true));
      std::array<int16_t,160> audio{};
      for(int block=0;block<100;++block) {
        assert(smart_speaker_voice_audio_read_input(audio.data(),audio.size(),100));
        for(auto sample:audio)assert(sample==(right?8000:4000));
      }
      assert(raw_frames_read>=rate && raw_frames_read<=rate+3); // One second plus bounded fractional carry.
      assert(!g_reconfigure_pending && mutes==0 && reconfigures==0);
      assert(smart_speaker_voice_audio_enable_input(false));
      assert(!smart_speaker_voice_audio_read_input(audio.data(),audio.size(),100));
    }
  }
  std::array<int16_t,160> audio{};
  assert(smart_speaker_voice_audio_enable_input(true));
  read_fault=true;assert(!smart_speaker_voice_audio_read_input(audio.data(),audio.size(),100));read_fault=false;
  change_during_read=true;assert(!smart_speaker_voice_audio_read_input(audio.data(),audio.size(),100));
  g_microphone_clock_rate=16000;++g_microphone_clock_generation;
  assert(smart_speaker_voice_audio_read_input(audio.data(),audio.size(),100));
  assert(!smart_speaker_voice_audio_read_input(audio.data(),audio.size(),0));
  // Closing an idle voice output must not mute the music owner.
  assert(smart_speaker_voice_audio_enable_output(false));assert(mutes==0 && reconfigures==0);
  g_microphone_clock_rate=44100;g_music_sample_rate=44100;
  assert(smart_speaker_voice_audio_enable_output(true));assert(g_microphone_clock_rate==16000);
  assert(smart_speaker_voice_audio_enable_output(false));assert(g_microphone_clock_rate==44100);
  assert(mutes==1 && reconfigures==2);
  assert(smart_speaker_voice_audio_enable_output(false));assert(mutes==1 && reconfigures==2);
  // A changed playback intent's latest clock wins over the old music rate.
  assert(smart_speaker_voice_audio_enable_output(true));g_music_sample_rate=48000;
  assert(smart_speaker_voice_audio_enable_output(false));assert(g_microphone_clock_rate==48000);
  // A timed-out clock request remains owned until close restores/cancels it.
  clock_timeout=true;assert(!smart_speaker_voice_audio_enable_output(true));
  assert(g_voice_output_enabled && g_reconfigure_pending && g_pending_sample_rate==16000);
  assert(smart_speaker_voice_audio_enable_output(false));
  assert(!g_reconfigure_pending && g_microphone_clock_rate==48000 && g_pending_sample_rate==48000);
  MicrophoneResampler filter;filter.reset(48000);int16_t output_sample[2]{};
  assert(filter.push(3000,output_sample)==0);assert(filter.push(0,output_sample)==0);
  assert(filter.push(-3000,output_sample)==1 && output_sample[0]==0);
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-microphone-') as folder:
    root=Path(folder);cpp=root/'test.cpp';cpp.write_text(fixture)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-O2',
                    '-I'+str(ROOT/'main/smart_speaker'),str(cpp),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
print('Microphone: production capture at 8/16/22.05/32/44.1/48 kHz, slot selection, exact duration, clock/fault boundaries and music-rate restoration passed')
