"""Exercise the production rescan command after a failed startup mount."""
from pathlib import Path
import subprocess, tempfile
source = (Path(__file__).resolve().parents[1] / 'main/smart_speaker/smart_speaker_runtime.cpp').read_text()
block = source.split('  case CommandType::kRescan:')[1].split('  case CommandType::kCycleMode:')[0]
helper = source[source.index('bool recover_sdcard_mount()'):source.index('\nvoid rescan_locked()')] if 'bool recover_sdcard_mount()' in source else ''
fixture = r'''
#include <cassert>
#include <mutex>
using TickType_t = unsigned;
#define pdMS_TO_TICKS(x) (x)
int attempts=0, failures=0, loads=0, scans=0, delays=0;
bool mounted=false;
void vTaskDelay(unsigned) { ++delays; }
namespace lunaredu::device {
bool tfcard_store_ready() { return mounted; }
bool tfcard_store_mount() { ++attempts; return mounted=attempts>failures; }
}
namespace product { bool desktop_resources_load() { ++loads; return true; } }
struct {std::mutex mutex;} g_runtime;
void rescan_locked() { ++scans; }
''' + helper + '\nvoid rescan() { do {\n' + block + r'''
} while(false); }
int main() {
 failures=1;rescan();assert(mounted && attempts==2 && loads==1 && scans==1);
 attempts=loads=scans=delays=0;rescan();assert(attempts==0 && loads==1 && scans==1);
 mounted=false;attempts=loads=scans=delays=0;failures=10;
 rescan();assert(!mounted && attempts==3 && loads==0 && scans==1 && delays==2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(fixture)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('SD recovery: transient mount, ready card, bounded persistent failure and wallpaper reload passed')
