"""Exercise production input admission and per-tick dispatch under a burst."""
from pathlib import Path
import re
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
s = (root/'main/smart_speaker/smart_speaker_runtime.cpp').read_text()
h = (root/'main/smart_speaker/runtime_internal.h').read_text()
commands = h[h.index('enum class CommandType'):h.index('\nstruct Runtime')]
enqueue = s[s.index('bool enqueue_command('):s.index('\nuint32_t now_ms()')]
tick = s[s.index('void tick_runtime()'):s.index('\nRuntimeStatus status_snapshot()')]
dispatch = tick[tick.index('  Command command;'):tick.rindex('}')]
fixture = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <deque>
''' + commands + r'''
constexpr int kMaxVolume=70, pdTRUE=1;
std::deque<Command> queue;
void *g_command_queue=&queue;
std::atomic<int> g_pending_volume_delta{0};
std::atomic<uint32_t> g_visual_activity{0};
namespace character {enum class Action {None,Volume,Previous,Next,Play,Pause,Stop};}
int xQueueSend(void *,const Command *c,int){if(queue.size()>=12)return 0;queue.push_back(*c);return 1;}
int xQueueReceive(void *,Command *c,int){if(queue.empty())return 0;*c=queue.front();queue.pop_front();return 1;}
int processed=0, volume_delta=0;
void process_command(Command c){++processed;if(c.type==CommandType::kAdjustVolume)volume_delta+=c.value;}
''' + enqueue + '\nvoid tick(){\n' + dispatch + r'''
}
int main(){
 for(int i=0;i<10000;++i)assert(enqueue_command(CommandType::kAdjustVolume,1,0));
 assert(queue.empty()&&g_pending_volume_delta==70);
 assert(enqueue_command(CommandType::kStop,0,0));
 tick();assert(processed==2&&volume_delta==70&&queue.empty());
 processed=volume_delta=0;
 for(int i=0;i<12;++i)assert(enqueue_command(CommandType::kNext,0,0));
 assert(!enqueue_command(CommandType::kNext,0,0));
 assert(enqueue_command(CommandType::kAdjustVolume,INT_MIN,0));
 tick();assert(queue.size()==11&&processed==2&&volume_delta==-70);
 assert(enqueue_command(CommandType::kAdjustVolume,INT_MAX,0));
 assert(enqueue_command(CommandType::kAdjustVolume,INT_MAX,0));
 tick();assert(queue.size()==10&&volume_delta==0);
 tick();assert(queue.size()==9&&processed==5);
 g_command_queue=nullptr;assert(!enqueue_command(CommandType::kAdjustVolume,1,0));
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-input-budget-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(fixture)
 subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Input budget: 10,000 rotary updates coalesce; transport queue progresses; work stays bounded')
