"""Reproduce launcher -> home with the production display cache gate."""
from pathlib import Path
import subprocess
import tempfile
source=(Path(__file__).resolve().parents[1]/'main/smart_speaker/smart_speaker_display.cpp').read_text()
start=source.index('  if (g_local_overlay == overlay && mode == previous_mode')
end=source.index('  previous_info_page =',start)
gate=source[start:end]
fixture=r'''
#include <cassert>
struct Menu {bool binding=false,open=false,information=false,network=false,library=false;int selected=0,information_page=0,library_index=0,network_selected=0,account_selected=0;bool unbind_confirm=false,unbind_pending=false;} menu;
int mode=0,previous_mode=0,previous_selection=0,previous_info_page=0,previous_library_index=0,previous_network_selected=0,previous_account_selected=0;
bool previous_unbind_confirm=false,previous_unbind_pending=false,g_local_overlay=false,panel_visible=false,overlay=false;
unsigned now=100,binding_update_ms=0;
void refresh(){
'''+gate+r'''
 panel_visible=menu.open;
 g_local_overlay=menu.open;
}
int main(){
 refresh();assert(!panel_visible);
 // Launcher has made the shared panel visible without changing legacy cache.
 panel_visible=true;g_local_overlay=true;
 // Navigation/button tests already prove Desktop and menu flags are cleared.
 refresh();assert(!panel_visible && !g_local_overlay);
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-menu-display-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(fixture)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Display cache: launcher back/home clears the shared panel before reusing legacy state')
