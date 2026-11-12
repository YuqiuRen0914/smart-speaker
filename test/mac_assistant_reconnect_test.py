"""Each real MQTT-connected event must subscribe and open a Mac session."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'lunaros_apps/lunaredu_pet/src/interaction/conversation/assistant_loop.cpp').read_text()
start = source.index('void handle_transport_connected_wait_session()')
end = source.index('\nvoid handle_connect_failure(', start)
fixture = r'''
#include <cassert>
#include <memory>
#include <mutex>
constexpr bool kAssistantUsesMac=true;
int subscribed=0,opened=0,waiting=0,failures=0;
struct NetworkMqttSession {bool Subscribe(){++subscribed;return true;}};
struct {std::mutex mutex;struct{bool cancelled=false;}conversation;
 struct{bool connected=false;std::shared_ptr<NetworkMqttSession> mqtt_session=std::make_shared<NetworkMqttSession>();}transport;}g_runtime;
void mark_transport_connected_waiting_session_locked(){++waiting;g_runtime.transport.connected=true;}
bool reopen_mqtt_audio_channel(){++opened;return true;}
enum class AssistantEventType{ConnectFailure};
struct Event{AssistantEventType type;const char*text;const char*detail;bool flag;};
bool post_assistant_event_locked(Event){++failures;return true;}
''' + source[start:end] + r'''
int main(){
 handle_transport_connected_wait_session();
 assert(waiting==1&&subscribed==1&&opened==1);
 g_runtime.transport.connected=false;
 handle_transport_connected_wait_session(); // library reconnect; Start() is not called again
 assert(waiting==2&&subscribed==2&&opened==2&&failures==0);
 g_runtime.conversation.cancelled=true;
 handle_transport_connected_wait_session();
 assert(subscribed==2&&opened==2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'test.cpp';cpp.write_text(fixture);binary=Path(tmp)/'test'
    subprocess.run(['c++','-std=c++17',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Mac reconnect: initial connect and library auto-reconnect handshake PASS')
