"""Exercise the production request -> cancel/relisten -> session cleanup chain."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PET = ROOT / 'lunaros_apps/lunaredu_pet/src'
loop = (PET / 'interaction/conversation/assistant_loop.cpp').read_text()
reset = (PET / 'interaction/conversation/assistant_runtime_reset.cpp').read_text()
types = (PET / 'interaction/conversation/assistant_runtime_types.h').read_text()


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


conversation = re.search(r'struct ConversationState \{.*?\n\};', types, re.S).group()
fixture = r'''
#include "interaction/conversation/assistant_session_state.h"
#include "interaction/conversation/assistant_events.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
using namespace lunaredu::pet::conversation::internal;
enum class StandbyListenMode { WakeWord, ContinuousConversation };
'''
fixture += conversation
fixture += r'''
struct Transport {
  struct Wire {std::string activity_id,open_request;} wire;
  bool connected=true, connect_pending=false, announced=true;
  std::string session_id="conversation-42";
  int64_t connect_started_at_ms=0;
  void *udp_channel=reinterpret_cast<void *>(1);
  struct Channel { void reset() {} } mqtt_udp_channel;
};
struct Runtime {
  std::mutex mutex;
  bool service_initialized=true,service_enabled=true,service_suspended=false;
  AssistantSessionState session_state=AssistantSessionState::ReadyIdle;
  ConversationState conversation;
  Transport transport;
  std::atomic<bool> close_audio_channel_requested{false};
  struct Activation {bool ready=true,pending=false;std::string code;} activation;
  struct Diagnostics {std::string last_error,last_binary_error,last_playback_error,last_message_summary;} diagnostics;
  struct Vad {
    bool voice_activity_active=false;
    int pcm_frames=0,speech_frames=0,silence_frames=0,last_rms=0,last_peak=0,
        last_raw_rms=0,last_raw_peak=0,last_raw_mean=0,current_utterance_ms=0,speech_ms=0,silence_ms=0;
  } vad;
  std::vector<std::string> pending_mcp_messages;
  std::deque<AssistantEvent> pending_events;
} g_runtime;
constexpr size_t kAssistantEventQueueCapacity=32;
constexpr int64_t kWakeWordResumeDelayMs=600;
constexpr const char *kTag="test";
int64_t now_ms(){return 1000;}
template<typename... T> void log_stub(T&&...){}
#define ESP_LOGI(...) log_stub(__VA_ARGS__)
#define ESP_LOGW(...) log_stub(__VA_ARGS__)
template<typename... T> void conversation_emit_status(T&&...){}
bool microphone=true, network=true, focus_allowed=true, cancel_during_focus=false;
int focus_releases=0, focus_requests=0, drained=0, quiesced=0, starts=0;
bool focus_held=false, input=false, output=false;
std::vector<std::string> messages;
namespace lunaredu::pet::conversation {
bool assistant_microphone_enabled(){return microphone;}
namespace audio_io {
void enable_input(bool active){input=active;}
void enable_output(bool active){output=active;}
void clear_output_reference(){}
}
}
bool assistant_network_connected(){return network;}
bool session_start_inflight_locked(){return g_runtime.conversation.start_pending;}
bool session_connect_inflight_locked(){return g_runtime.transport.connect_pending;}
bool session_identity_available_locked(){return !g_runtime.transport.session_id.empty();}
bool session_audio_path_ready_locked(){return g_runtime.transport.udp_channel!=nullptr;}
bool session_listening_locked(){return g_runtime.session_state==AssistantSessionState::Listening;}
void set_session_start_inflight_locked(bool active){g_runtime.conversation.start_pending=active;}
void set_session_connect_inflight_locked(bool active){g_runtime.transport.connect_pending=active;}
void set_session_has_announcement_locked(bool active){g_runtime.transport.announced=active;}
void session_reset_supporting_facts_locked(){set_session_start_inflight_locked(false);set_session_connect_inflight_locked(false);}
void drain_downlink_audio_queue(){++drained;}
void quiesce_audio(){++quiesced;input=output=false;}
void conversation_release_audio_focus(const char *){++focus_releases;focus_held=false;}
bool request_conversation_control(bool listen);
bool conversation_request_audio_focus(const char *,int) {
  ++focus_requests;
  if(cancel_during_focus){cancel_during_focus=false;request_conversation_control(false);}
  if(focus_allowed)focus_held=true;
  return focus_allowed;
}
std::string xiaozhi_build_listen_stop_message(const std::string &id){return "stop:"+id;}
std::string xiaozhi_build_abort_message(const std::string &id){return "abort:"+id;}
bool send_service_text(const std::string &message){messages.push_back(message);return true;}
enum class ConnectServiceAction {ReadyIdle,StartConnecting};
ConnectServiceAction derive_connect_service_action_locked(){return session_identity_available_locked()?ConnectServiceAction::ReadyIdle:ConnectServiceAction::StartConnecting;}
bool apply_connect_service_action_locked(ConnectServiceAction,const char *){++starts;return true;}
void enter_speaking_state_locked(bool,const char *,const char *){g_runtime.session_state=AssistantSessionState::Speaking;output=true;}
'''
fixture += function(reset, 'void close_audio_channel_locked(')
fixture += function(loop, 'void handle_manual_start_requested(')
fixture += function(loop, 'void handle_conversation_control(')
fixture += function(loop, 'void handle_tts_started()')
fixture += function(loop, 'bool request_conversation_control(')
fixture += r'''
void reset(AssistantSessionState phase) {
  g_runtime.conversation={};g_runtime.transport={};g_runtime.session_state=phase;
  g_runtime.close_audio_channel_requested=false;g_runtime.pending_events.clear();
  g_runtime.service_enabled=true;g_runtime.service_suspended=false;
  microphone=network=focus_allowed=true;cancel_during_focus=false;
  focus_releases=focus_requests=drained=quiesced=starts=0;
  input=output=focus_held=true;messages.clear();
}
void consume_control() {
  assert(!g_runtime.pending_events.empty());
  const auto event=g_runtime.pending_events.front();g_runtime.pending_events.pop_front();
  assert(event.type==AssistantEventType::ConversationControlRequested);
  handle_conversation_control(event);
}
int main() {
  for(auto phase:{AssistantSessionState::Opening,AssistantSessionState::Listening,
                 AssistantSessionState::ReadyIdle,AssistantSessionState::Speaking}) {
    reset(phase);g_runtime.conversation.awaiting_response=phase==AssistantSessionState::ReadyIdle;
    g_runtime.conversation.start_pending=phase==AssistantSessionState::Opening;
    const auto old_generation=g_runtime.conversation.generation;
    g_runtime.pending_events.push_back({AssistantEventType::TtsStarted, {}, {}, false});
    assert(request_conversation_control(false));
    assert(g_runtime.conversation.cancelled && g_runtime.conversation.control_pending);
    assert(g_runtime.conversation.generation!=old_generation);
    assert(!g_runtime.conversation.accepts_response(phase));
    consume_control();
    assert(quiesced==1 && drained==1 && !input && !output);
    assert(!focus_held && focus_releases==1 && !g_runtime.conversation.start_pending);
    assert(!g_runtime.conversation.awaiting_response && !g_runtime.conversation.control_pending);
    assert(g_runtime.transport.session_id=="conversation-42");
    assert(messages==std::vector<std::string>({"stop:conversation-42","abort:conversation-42"}));
    handle_tts_started();assert(g_runtime.session_state!=AssistantSessionState::Speaking && !output);
  }
  reset(AssistantSessionState::Speaking);
  g_runtime.conversation.transcript_text="previous context";
  assert(request_conversation_control(true));consume_control();
  assert(quiesced==1 && drained==1 && focus_releases==0 && focus_held);
  assert(starts==1 && g_runtime.conversation.start_pending && !g_runtime.conversation.cancelled);
  assert(g_runtime.transport.session_id=="conversation-42" && !g_runtime.close_audio_channel_requested);
  assert(g_runtime.conversation.transcript_text=="previous context" && g_runtime.conversation.listening_mode=="manual");
  // A late response cannot interrupt the replacement listening turn.
  g_runtime.conversation.start_pending=false;g_runtime.session_state=AssistantSessionState::Listening;
  handle_tts_started();assert(g_runtime.session_state==AssistantSessionState::Listening && !output);
  assert(request_conversation_control(true) && g_runtime.pending_events.empty()); // idempotent
  // After fresh speech ends, its own response is accepted.
  g_runtime.conversation.awaiting_response=true;handle_tts_started();
  assert(g_runtime.session_state==AssistantSessionState::Speaking && output);
  reset(AssistantSessionState::ReadyIdle);
  assert(request_conversation_control(false) && g_runtime.pending_events.empty());
  assert(!g_runtime.conversation.control_pending); // Home hold cannot flash voice UI.
  // Cancel supersedes an unconsumed start, even with a full background queue.
  reset(AssistantSessionState::ReadyIdle);assert(request_conversation_control(true));
  for(int i=0;i<31;++i)g_runtime.pending_events.push_back({AssistantEventType::TtsStarted, {}, {}, false});
  assert(request_conversation_control(false));assert(g_runtime.pending_events.size()==1);consume_control();assert(starts==0);
  // An idle background transport connection is not an active user start.
  reset(AssistantSessionState::Opening);assert(request_conversation_control(true));consume_control();assert(starts==1);
  // A failure to restart releases the existing focus instead of stranding music.
  for(int failure=0;failure<3;++failure) {
    reset(AssistantSessionState::Speaking);assert(request_conversation_control(true));
    if(failure==0)network=false;if(failure==1)microphone=false;if(failure==2)focus_allowed=false;
    consume_control();assert(!focus_held && starts==0 && !g_runtime.conversation.control_pending);
  }
  // A cancellation arriving during the unlocked focus request invalidates commit.
  reset(AssistantSessionState::Speaking);cancel_during_focus=true;
  assert(request_conversation_control(true));consume_control();assert(starts==0);
  consume_control();assert(!focus_held && !input && !output);
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-assistant-control-') as directory:
    source = Path(directory) / 'test.cpp'
    source.write_text(fixture)
    binary = Path(directory) / 'test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-I'+str(PET), str(source),
                    str(PET/'interaction/conversation/assistant_session_state.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Assistant controls: production cancellation in all phases, late TTS rejection, context-preserving restart, queue pressure and focus races passed')
