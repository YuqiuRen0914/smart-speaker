"""Compile the actual button polling routine with host GPIO/network doubles."""
from pathlib import Path
import re
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
source = (app / 'main/smart_speaker/smart_speaker_runtime.cpp').read_text(encoding='utf-8')
header = (app / 'main/smart_speaker/smart_speaker_runtime.h').read_text(encoding='utf-8')
menu = re.search(r'struct LocalMenuState \{.*?\n\};', header, re.S).group()
button = re.search(r'struct LocalButtonState \{.*?\n\};', source, re.S).group()
encoder = source[source.index('void sync_local_library_locked('):source.index('void poll_encoder()')]
routine_end = source.index('void process_touch_gestures()') if 'void process_touch_gestures()' in source else source.index('void ota_lifecycle(')
routine = source[source.index('void poll_local_buttons() {'):routine_end]
prefix = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>
#define CONFIG_LUNAREDU_SMART_SPEAKER_BUTTON_ACTIVE_LOW 1
#define CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO 1
#define CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO 2
#define ESP_LOGI(...) ((void)0)
using gpio_num_t = int;
enum class NetworkProvisioningState { Idle, Active, Applying };
enum class NetworkLinkState { Down, Connecting, Up };
enum class DeviceLinkBindingState { Bound, Pairing, Connecting, Error, Unbound };
enum class DeviceLinkUnbindState { Idle, Pending, Succeeded, Failed };
DeviceLinkUnbindState unbind_state = DeviceLinkUnbindState::Idle;
DeviceLinkUnbindState device_link_unbind_state() { return unbind_state; }
int unbind_requests = 0, pairing_requests = 0;
struct Network { NetworkProvisioningState provisioning = NetworkProvisioningState::Idle; NetworkLinkState link = NetworkLinkState::Up; } net;
NetworkProvisioningState network_provisioning_state() { return net.provisioning; }
NetworkLinkState network_link_state() { return net.link; }
struct Pairing { DeviceLinkBindingState state = DeviceLinkBindingState::Error; int64_t expires_at_ms = 0; } pairing;
Pairing device_link_pairing_snapshot() { return pairing; }
bool request_device_link_unbind() {
  if (pairing.state != DeviceLinkBindingState::Bound || net.link != NetworkLinkState::Up ||
      unbind_state == DeviceLinkUnbindState::Pending) return false;
  ++unbind_requests; unbind_state = DeviceLinkUnbindState::Pending; return true;
}
bool dismiss_transfer() { return false; }
bool cancel_device_link_pairing() { return true; }
bool request_device_link_pairing() { ++pairing_requests; return true; }
struct TransferStatus { bool receiving=false, batch_open=false; } transfer;
TransferStatus transfer_snapshot() { return transfer; }
enum class CommandType { kNext, kPrevious, kPlayTrack, kCycleMode };
int mode_changes = 0;
int tracks = 0, queued_index = -1; uint32_t queued_revision = 0;
bool enqueue_command(CommandType type, int index = 0, uint32_t revision = 0) { if(type==CommandType::kCycleMode)++mode_changes;else ++tracks; queued_index=index; queued_revision=revision; return true; }
struct TrackInfo {std::string name, path;};
struct Runtime { std::mutex mutex; std::vector<TrackInfo> playlist; int current_index=0; uint32_t playlist_revision=1; bool uploading=false, audio_ready=true; } g_runtime;
uint32_t clock_ms = 0;
bool left_down = false, right_down = false;
uint32_t now_ms() { return clock_ms; }
int gpio_get_level(int pin) { return (pin == 1 ? left_down : right_down) ? 0 : 1; }
'''
state = r'''
LocalMenuState g_menu;
LocalButtonState g_local_left, g_local_right;
std::mutex g_menu_mutex;
std::atomic<bool> g_provisioning_requested{false}, g_provisioning_cancel_requested{false};
bool g_provisioning_observed = false, g_pairing_retry = false;
'''
tests = r'''
void tick(uint32_t ms = 1) { clock_ms += ms; poll_local_buttons(); }
void click(bool right) {
  (right ? right_down : left_down) = true; tick(); tick(31);
  (right ? right_down : left_down) = false; tick(); tick(31);
}
void reset(uint32_t start = 0) {
  mode_changes=0; transfer={};
  pairing = {}; unbind_state = DeviceLinkUnbindState::Idle; unbind_requests = pairing_requests = 0;
  g_runtime.playlist = {{"a.mp3","/a"},{"b.mp3","/b"},{"c.mp3","/c"},{"d.mp3","/d"}};
  g_runtime.current_index=1; g_runtime.playlist_revision=1; g_runtime.uploading=false; g_runtime.audio_ready=true;
  queued_index=-1; queued_revision=0;
  g_menu = {}; g_local_left = {}; g_local_right = {}; net = {};
  g_provisioning_requested = false; g_provisioning_cancel_requested = false;
  g_provisioning_observed = false; g_pairing_retry = false;
  left_down = right_down = false; tracks = 0; clock_ms = start;
}
void open_menu() {
  right_down = true; tick(); tick(31); tick(1500);
  assert(g_menu.open && g_menu.hold_percent == 0);
  right_down = false; tick(); tick(31); assert(tracks == 0);
}
void enter_setup() {
  open_menu(); click(true);
  assert(g_menu.network && !g_provisioning_requested);
  click(true); assert(g_menu.network && !g_provisioning_requested);
  route_local_encoder(-1); click(true);
  assert(!g_menu.open && g_menu.provisioning && g_provisioning_requested);
}
int main() {
  for (uint32_t start : {0U, 0xfffffff0U}) {
    reset(start);click(false);assert(tracks==1 && mode_changes==0);
    reset(start);left_down=true;tick();tick(31);tick(400);
    assert(g_menu.mode_hold_percent==0);tick(100);assert(g_menu.mode_hold_percent>0);
    tick(1000);assert(mode_changes==1 && tracks==0 && !g_menu.open);
    tick(3000);assert(mode_changes==1);left_down=false;tick();tick(31);
    assert(mode_changes==1 && tracks==0 && g_menu.mode_hold_percent==0);
    reset(start);open_menu();left_down=true;tick();tick(31);tick(2000);
    left_down=false;tick();tick(31);assert(!g_menu.open && mode_changes==0 && tracks==0);
    reset(start);transfer.receiving=true;left_down=true;tick();tick(31);tick(2000);
    assert(mode_changes==0 && g_menu.mode_hold_percent==0);
    reset(start);left_down=right_down=true;tick();tick(31);tick(2000);
    left_down=right_down=false;tick();tick(31);assert(mode_changes==0 && !g_menu.open && tracks==0);
  }
  // Account view is safe to confirm/leave; logout requires explicit selection
  // and a second confirmation with Cancel selected by default.
  reset(); pairing.state=DeviceLinkBindingState::Bound;open_menu();route_local_encoder(-1);click(true);
  assert(g_menu.binding && pairing_requests==0);
  click(true);assert(!g_menu.unbind_confirm && unbind_requests==0);
  click(false);assert(g_menu.open && !g_menu.binding);click(true);
  route_local_encoder(-1);click(true);assert(g_menu.unbind_confirm && g_menu.account_selected==0);
  click(true);assert(!g_menu.unbind_confirm && unbind_requests==0);
  click(true);route_local_encoder(-1);click(false);
  assert(!g_menu.unbind_confirm && g_menu.binding && unbind_requests==0);
  click(true);route_local_encoder(-1);click(true);
  assert(g_menu.unbind_pending && unbind_requests==1);
  click(true);route_local_encoder(-1);click(true);assert(unbind_requests==1);
  click(false);assert(g_menu.open);click(true);assert(g_menu.unbind_pending && pairing_requests==0);
  click(true);assert(unbind_requests==1);
  // A queued request stays pending while disconnected, but Back always works.
  net.link=NetworkLinkState::Down;tick(60000);assert(g_menu.unbind_pending);
  click(false);assert(g_menu.open);net.link=NetworkLinkState::Up;
  unbind_state=DeviceLinkUnbindState::Succeeded;pairing.state=DeviceLinkBindingState::Unbound;tick();
  assert(!g_menu.unbind_pending && g_menu.account_notice=="已退出账号");
  click(true);assert(g_menu.binding && pairing_requests==0);click(true);assert(pairing_requests==1);
  // Failure must not claim successful logout, including after a presence refresh.
  reset();pairing.state=DeviceLinkBindingState::Bound;g_menu.binding=true;g_menu.account_selected=1;
  click(true);route_local_encoder(-1);click(true);unbind_state=DeviceLinkUnbindState::Failed;tick();
  assert(!g_menu.unbind_pending && g_menu.account_notice=="退出未完成");
  click(false);click(false);assert(!g_menu.open && !g_menu.binding);
  // Connectivity/account changes between the two confirmations reject the action.
  for (bool offline : {true,false}) {
    reset();pairing.state=DeviceLinkBindingState::Bound;g_menu.binding=true;g_menu.account_selected=1;
    click(true);route_local_encoder(-1);
    if (offline) net.link=NetworkLinkState::Down;else pairing.state=DeviceLinkBindingState::Unbound;
    click(true);assert(!g_menu.unbind_pending && !g_menu.unbind_confirm && unbind_requests==0);
    click(false);assert(g_menu.open);
  }
  reset(); assert(!route_local_encoder(1)); open_menu();
  assert(route_local_encoder(1) && g_menu.selected == 3);
  assert(route_local_encoder(-1) && g_menu.selected == 0);
  assert(route_local_encoder(-1) && g_menu.selected == 1);
  assert(route_local_encoder(-1) && g_menu.selected == 2);
  click(true);
  assert(g_menu.information && !g_menu.open && tracks == 0);
  assert(route_local_encoder(1) && g_menu.information_page == 2);
  assert(route_local_encoder(-1) && g_menu.information_page == 0);
  for (int page : {1, 2, 0}) {
    click(true); assert(g_menu.information_page == page && tracks == 0);
  }
  right_down = true; tick(); tick(31); tick(1600);
  assert(g_menu.information && !g_menu.open && g_menu.hold_percent == 0);
  right_down = false; tick(); tick(31);
  click(false); assert(!g_menu.information && g_menu.open && g_menu.selected == 2);
  click(false); assert(!g_menu.open && tracks == 0);

  // Network status entry never starts/restarts provisioning by itself.
  for (auto link : {NetworkLinkState::Up, NetworkLinkState::Down, NetworkLinkState::Connecting}) {
    reset(); net.link=link; open_menu(); click(true);
    assert(g_menu.network && !g_provisioning_requested && !g_menu.provisioning);
    right_down=true; tick(); tick(31); tick(1600);
    assert(g_menu.network && !g_menu.open && g_menu.hold_percent==0);
    right_down=false; tick(); tick(31); assert(!g_provisioning_requested);
    route_local_encoder(-1); assert(g_menu.network_selected==1);
    route_local_encoder(1); assert(g_menu.network_selected==0);
    click(false);assert(g_menu.open);click(false);assert(!g_menu.open && tracks==0);
  }
  reset();open_menu();route_local_encoder(1);click(true);
  assert(g_menu.library && g_menu.library_index==1 && !g_menu.open);
  assert(route_local_encoder(-1) && g_menu.library_index==2);
  click(true);assert(!g_menu.library && queued_index==2 && queued_revision==1 && tracks==1);
  reset();open_menu();route_local_encoder(1);click(true);
  route_local_encoder(1);assert(g_menu.library_index==0);
  route_local_encoder(1);assert(g_menu.library_index==3);
  click(false);assert(g_menu.open && !g_menu.library && g_menu.selected==3);
  click(false);assert(!g_menu.open && tracks==0);
  reset();open_menu();route_local_encoder(1);click(true);
  // Delete a different row while browsing: selected path stays the same.
  g_runtime.playlist.erase(g_runtime.playlist.begin());++g_runtime.playlist_revision;
  click(true);assert(queued_index==0 && queued_revision==2 && !g_menu.library);
  reset();open_menu();route_local_encoder(1);click(true);
  // Deleted selection must not play the replacement row on confirmation.
  g_runtime.playlist.erase(g_runtime.playlist.begin()+1);++g_runtime.playlist_revision;
  click(true);assert(g_menu.library && tracks==0 && !g_menu.library_notice.empty());
  reset();g_runtime.playlist.clear();open_menu();route_local_encoder(1);click(true);
  route_local_encoder(-1);click(true);assert(g_menu.library && tracks==0);
  click(false);assert(g_menu.open);
  reset();open_menu();route_local_encoder(1);click(true);
  g_runtime.uploading=true;click(true);assert(g_menu.library && tracks==0);

  for (uint32_t start : {0U, 0xfffffff0U}) {
    for (uint32_t duration : {40U, 180U, 350U, 399U}) {
      reset(start);
      right_down = true; tick(); tick(31);
      for (uint32_t ms = 0; ms < duration; ++ms) {
        tick(); assert(g_menu.hold_percent == 0 && !g_menu.open);
      }
      right_down = false; tick(); tick(31);
      assert(tracks == 1 && g_menu.hold_percent == 0 && !g_menu.open);
    }
    reset(start); right_down = true; tick(); tick(31); tick(500);
    assert(g_menu.hold_percent > 0 && !g_menu.open);
    tick(999); assert(!g_menu.open);
    right_down = false; tick(); tick(31); // Release debounce must not open settings.
    assert(!g_menu.open && tracks == 1);
    reset(start); open_menu();
    right_down = true; tick(); tick(31); tick(500);
    assert(g_menu.hold_percent == 0 && g_menu.open); // Confirm never starts a morph.
  }
  for (uint32_t start : {0U, 0xfffffff0U}) {
    reset(start); enter_setup();
    // Cancel before the network executor has even started the AP.
    click(false);
    assert(g_menu.open && !g_menu.provisioning);
    assert(!g_provisioning_requested && g_provisioning_cancel_requested && tracks == 0);
    tick(); click(false); assert(!g_menu.open && tracks == 0);
  }
  for (auto phase : {NetworkProvisioningState::Active, NetworkProvisioningState::Applying}) {
    reset(); enter_setup(); g_provisioning_requested = false;
    net.provisioning = phase; net.link = NetworkLinkState::Down; tick();
    // Right press/hold has no hidden track/menu action during setup.
    click(true); right_down = true; tick(); tick(31); tick(1700);
    assert(!g_menu.open && g_menu.hold_percent == 0 && tracks == 0);
    right_down = false; tick(); tick(31);
    click(false);
    assert(g_menu.open && g_menu.provisioning_dismissed && !g_menu.provisioning);
    click(false); assert(!g_menu.open && tracks == 0); // Exit again while stop is pending.
    net.provisioning = NetworkProvisioningState::Idle;
    net.link = NetworkLinkState::Connecting; tick();
    assert(!g_menu.provisioning && !g_menu.provisioning_dismissed);
    click(true); assert(tracks == 1); // Normal controls return before association finishes.
  }
  reset(); enter_setup(); g_provisioning_requested = false;
  net.provisioning = NetworkProvisioningState::Active; net.link = NetworkLinkState::Down; tick();
  net.provisioning = NetworkProvisioningState::Idle; tick(); // Failed connection can still be canceled.
  assert(g_menu.provisioning); click(false); assert(g_menu.open && !g_menu.provisioning);
  reset(); enter_setup(); g_provisioning_requested = false;
  net.provisioning = NetworkProvisioningState::Applying; net.link = NetworkLinkState::Connecting; tick();
  net.provisioning = NetworkProvisioningState::Idle; net.link = NetworkLinkState::Up; tick();
  assert(!g_menu.provisioning); click(false); assert(tracks == 1);
  reset(); net.provisioning = NetworkProvisioningState::Active; net.link = NetworkLinkState::Down;
  click(false); assert(g_menu.open && g_provisioning_cancel_requested); // Boot-time automatic AP.
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-menu-') as temp:
    root = Path(temp)
    test = root / 'test.cpp'
    test.write_text(prefix + menu + button + state + encoder + routine + tests, encoding='utf-8')
    binary = root / 'test.exe'
    subprocess.run(['g++', '-std=c++17','-D_WIN32_WINNT=0x0601','-include',str(app/'lunaros_apps/lunaredu_pet/test/host/mutex.h'), '-O2', '-Wall', '-Wextra', '-Werror', str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Local menu: pending/active/applying/failed setup cancel, return, success, auto-AP and clock-wrap passed')
