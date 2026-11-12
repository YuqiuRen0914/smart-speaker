"""Run the production active-capture branch: PCM -> VAD -> stop -> gain."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PET = ROOT / 'lunaros_apps/lunaredu_pet/src'
audio = (PET / 'interaction/voice/assistant_audio.cpp').read_text()
core = (PET / 'interaction/conversation/assistant_core.cpp').read_text()
types = (PET / 'interaction/conversation/assistant_runtime_types.h').read_text()
constants = (PET / 'interaction/conversation/assistant_runtime_constants.h').read_text()


def block(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


fixture = r'''
#include "interaction/conversation/assistant_turn_policy.h"
#include "interaction/conversation/assistant_events.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <mutex>
#include <vector>
using namespace lunaredu::pet::conversation::internal;
constexpr uint32_t kFrameDurationMs=60;
'''
fixture += '\n'.join(re.findall(r'inline constexpr [^;]+k(?:Vad\w+|Esp32Listening\w+)\s*=\s*[^;]+;', constants))
fixture += re.search(r'struct VoiceActivityState \{.*?\n\};', types, re.S).group()
fixture += r'''
namespace lunaredu::pet::conversation {struct WakeWordFrontendStatus {};}
using lunaredu::pet::conversation::WakeWordFrontendStatus;
struct Runtime {
  std::mutex mutex, wake_word_mutex;
  struct {bool afe_initialized=false;} wake_word;
  struct {WakeWordFrontendStatus status(){return {};}} wake_word_frontend;
  VoiceActivityState vad;
  struct {std::string listening_mode="manual";} conversation;
  struct {std::string last_error;} diagnostics;
} g_runtime;
int stops=0;
bool post_assistant_event_locked(AssistantEvent event) {
  assert(event.type==AssistantEventType::AutoStopRequested);++stops;return true;
}
void refresh_wake_word_status_locked(WakeWordFrontendStatus){}
'''
for signature in ['uint32_t pcm_abs_i16(', 'int16_t clamp_pcm_i32(', 'void apply_pcm_gain(',
                  'void remove_pcm_dc_bias(', 'void normalize_pcm_peak(']:
    fixture += block(core, signature)
fixture += block(audio, 'void observe_vad_frame_locked(')
fixture += 'bool capture(std::vector<int16_t> &s_mic_buffer) { bool should_listen=true, should_auto_stop=false;\n'
fixture += block(audio[audio.index('bool poll_local_audio_once()'):], 'if (should_listen) {')
fixture += 'return true;}\n'
fixture += r'''
void frame(int amplitude, int dc=0) {
  std::vector<int16_t> pcm(960);
  for(size_t i=0;i<pcm.size();++i)pcm[i]=dc+(i%2?amplitude:-amplitude);
  capture(pcm);
}
int main() {
  // These quiet frames exceed the gain threshold but are below the speech
  // threshold. Gain must never turn them into a speech event.
  for(int noise:{256,300,499}) {
    g_runtime.vad={};stops=0;
    for(int i=0;i<300;++i)frame(noise,1000);
    assert(!g_runtime.vad.utterance_started && stops==0);
    assert(should_timeout_empty_single_turn("manual",true,1000,
        g_runtime.vad.utterance_started,16000,15000));
    g_runtime.vad={};stops=0;
    for(int i=0;i<10;++i)frame(1000,1000);
    assert(g_runtime.vad.utterance_started && stops==0);
    for(int i=0;i<16;++i)frame(noise,1000);
    assert(stops==0);
    frame(noise,1000);assert(stops==1); // 1020 ms of quiet ends the turn
  }
  g_runtime.vad={};stops=0;
  std::vector<int16_t> pcm(960);
  for(size_t i=0;i<pcm.size();++i)pcm[i]=i%2?1000:-1000;
  assert(capture(pcm) && std::abs(pcm[0])==12000); // cloud still gets amplified speech
}
'''
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'capture.cpp'
    source.write_text(fixture)
    binary = Path(directory) / 'capture'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-I'+str(PET), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Assistant capture regression passed')
