"""Run the real microphone upload function with a captured transport envelope."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
audio = (root / 'lunaros_apps/lunaredu_pet/src/interaction/voice/assistant_audio.cpp').read_text()
start = audio.index('bool process_uplink_audio_work_item(')
end = audio.index('\nbool prime_uplink_audio_channel()', start)
fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
constexpr bool kAssistantUsesMac = MAC_TEST;
#define ESP_LOGI(...) ((void)0)
constexpr int ESP_AUDIO_ERR_OK=0;
enum class AssistantSessionState {Listening};
struct OpusAudioCodec {void *encoder=nullptr; int encoder_frame_samples=960, encoder_output_bytes=100;};
struct esp_audio_enc_in_frame_t {uint8_t *buffer; uint32_t len;};
struct esp_audio_enc_out_frame_t {uint8_t *buffer; uint32_t len, encoded_bytes, pts;};
int esp_opus_enc_process(void*, esp_audio_enc_in_frame_t*, esp_audio_enc_out_frame_t *out) {
  out->buffer[0]=42; out->encoded_bytes=1; return 0;
}
uint32_t stamp=0; int sends=0;
struct NetworkUdpChannel {int Send(const std::string &data){++sends;return data.size();}};
struct Wire {
  bool encrypt_payload(const uint8_t*, size_t, uint32_t ts, std::string &out){stamp=ts;out="frame";return true;}
  int last_crypto_error(){return 0;}
};
struct Runtime {
 std::mutex mutex;
 struct {bool cancelled=false, control_pending=false;} conversation;
 AssistantSessionState session_state=AssistantSessionState::Listening;
 struct {int speech_ms=0,silence_ms=0;bool voice_activity_active=false,utterance_started=false;} vad;
 struct {std::string last_error,last_transport_error,last_binary_error;} diagnostics;
 struct {uint32_t mac_turn_id=7;Wire mqtt_udp_channel;std::shared_ptr<NetworkUdpChannel> udp_channel=std::make_shared<NetworkUdpChannel>();} transport;
 OpusAudioCodec codec;
 uint32_t sent_audio_packets=0;
} g_runtime;
bool session_ready_locked(){return true;}
bool ensure_opus_encoder_locked(){return true;}
uint32_t now_ms(){return 314159;}
uint32_t read_udp_sequence(const std::string&){return 128;}
'''
fixture += audio[start:end]
fixture += r'''
int main(){
 std::vector<int16_t> pcm(960,1000);
 assert(process_uplink_audio_work_item(pcm));
 assert(sends==1 && stamp==(kAssistantUsesMac?7:314159));
 g_runtime.transport.mac_turn_id=8;
 assert(process_uplink_audio_work_item(pcm));
 assert(sends==2 && stamp==(kAssistantUsesMac?8:314159));
 g_runtime.conversation.cancelled=true;
 assert(!process_uplink_audio_work_item(pcm) && sends==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'capture.cpp'
    source.write_text(fixture)
    for mac in (0, 1):
        binary = Path(directory) / f'capture-{mac}'
        subprocess.run(['c++', '-std=c++20', '-O2', f'-DMAC_TEST={mac}',
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Microphone upload: Mac turn fence, official timestamp and cancellation PASS')
