"""Only overlapping LVGL flushes may request a direct music-art redraw."""
from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'main/smart_speaker/smart_speaker_display.cpp').read_text()
body=s[s.index('void notify_character_flush('):s.index('\nstd::string g_setup_cached_ssid;')]
code=r'''
#include <atomic>
#include <cstdint>
#include <cassert>
std::atomic<bool> g_character_damaged{false},g_music_damaged{false};
std::atomic<int> g_character_damage_top{50},g_character_damage_height{160};
''' + body + r'''
int main(){
 notify_character_flush(0,0,240,32);assert(!g_music_damaged);
 notify_character_flush(12,212,216,28);assert(!g_music_damaged);
 notify_character_flush(208,32,32,176);assert(!g_music_damaged);
 notify_character_flush(31,31,2,2);assert(g_music_damaged.exchange(false));
 notify_character_flush(0,0,240,240);assert(g_music_damaged.exchange(false));
 notify_character_flush(32,32,176,176);assert(g_music_damaged.exchange(false));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Music art damage: title/lyric flushes excluded, overlapping/full refresh repaired')
