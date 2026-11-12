"""Exercise production queue draining while voice owns the physical output."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/smart_speaker/smart_speaker_runtime.cpp').read_text()

def block(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

if 'void drain_playback_commands()' in source:
    production = block(source, 'void drain_playback_commands()')
else:
    start = source.index('  Command command;\n  // Bound work per UI tick')
    end = source.index('\n}\n\nRuntimeStatus status_snapshot()', start)
    production = 'void drain_playback_commands(){\n' + source[start:end] + '\n}'

fixture = r'''
#include <atomic>
#include <cassert>
#include <deque>
#include <mutex>
#include <vector>
enum class CommandType {kPlayTrack,kStop,kAssistantPause,kAssistantResume,kAdjustVolume};
struct Command {CommandType type;int value=0;unsigned revision=0;};
std::deque<Command> queue;
auto *g_command_queue=&queue;
std::mutex g_command_admission_mutex,g_player_operation_mutex;
struct {std::mutex mutex;bool assistant_focus_active=false;} g_runtime;
std::atomic<int> g_pending_volume_delta{0};
constexpr bool pdTRUE=true;
bool xQueuePeek(std::deque<Command>*q,Command*c,int){if(q->empty())return false;*c=q->front();return true;}
bool xQueueReceive(std::deque<Command>*q,Command*c,int){if(!xQueuePeek(q,c,0))return false;q->pop_front();return true;}
std::vector<CommandType> executed;
void process_command(const Command &c){executed.push_back(c.type);}
''' + production + r'''
int main(){
 // The actual bug: a tool queues music before the assistant's final TTS.
 g_runtime.assistant_focus_active=true;
 queue.push_back({CommandType::kPlayTrack});
 queue.push_back({CommandType::kStop});
 g_pending_volume_delta=2;
 for(int i=0;i<100;i++)drain_playback_commands();
 assert(executed.empty() && queue.size()==2 && g_pending_volume_delta==2);
 // Focus pause must still execute; admission puts this control at the front.
 queue.push_front({CommandType::kAssistantPause});
 drain_playback_commands();
 assert(executed.size()==1 && executed[0]==CommandType::kAssistantPause);
 assert(queue.size()==2 && g_pending_volume_delta==2);
 // Final playback drain releases voice; preserve command order, execute once.
 g_runtime.assistant_focus_active=false;
 drain_playback_commands();drain_playback_commands();drain_playback_commands();
 assert(queue.empty() && executed.size()==4);
 assert(executed[1]==CommandType::kPlayTrack);
 assert(executed[2]==CommandType::kAdjustVolume);
 assert(executed[3]==CommandType::kStop);
 assert(g_pending_volume_delta==0);
 // A busy player leaves accepted work in the queue, instead of dropping it.
 queue.push_back({CommandType::kPlayTrack});
 g_player_operation_mutex.lock();drain_playback_commands();g_player_operation_mutex.unlock();
 assert(queue.size()==1);drain_playback_commands();assert(queue.empty());
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'test.cpp'; cpp.write_text(fixture)
    binary = Path(directory) / 'test'
    subprocess.run(['c++', '-std=c++17', '-O2', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Music/voice queue: deferred tool execution, pause priority, order, busy retry PASS')
