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
#include "product_state_runtime.h"
#include "desktop_resources.h"
namespace product = smart_speaker::product;
bool catalog_ready = false;
int requested_pet_action = -1;
void smart_speaker_display_request_pet_action(int action) { requested_pet_action = action; }
namespace smart_speaker::product {
DesktopResourcesSnapshot desktop_resources_snapshot() { return {}; }
bool pet_animation_catalog_ready() { return catalog_ready; }
}
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
enum class CommandType { kNext, kPrevious, kPlayTrack, kCycleMode, kWhiteNoise };
int mode_changes = 0;
int tracks = 0, queued_index = -1; uint32_t queued_revision = 0;
bool enqueue_command(CommandType type, int index = 0, uint32_t revision = 0) { if(type==CommandType::kCycleMode)++mode_changes;else ++tracks; queued_index=index; queued_revision=revision; return true; }
struct TrackInfo {std::string name, path;};
struct Runtime { std::mutex mutex; std::vector<TrackInfo> playlist; int current_index=0; uint32_t playlist_revision=1; bool uploading=false, audio_ready=true; } g_runtime;
uint32_t clock_ms = 0;
bool left_down = false, right_down = false;
uint32_t now_ms() { return clock_ms; }
struct PlayerStatus {bool playing=false,paused=false,white_noise=false;};
PlayerStatus status_snapshot(){return {};}
void play(){}
void pause(){}
int assistant_starts=0, assistant_cancels=0;
bool xiaozhi_start_listening(){++assistant_starts;return true;}
bool xiaozhi_stop_listening(){++assistant_cancels;return true;}
bool xiaozhi_start_activation(){return true;}
namespace lunaredu::pet::conversation {
struct Snapshot {bool activation_ready=true;};
Snapshot snapshot(){return {};}
struct RuntimeSnapshot {bool interaction_active=false, activation_ready=true, opening=false, listening=false, thinking=false, speaking=false;} assistant;
RuntimeSnapshot runtime_snapshot(){auto result=assistant; result.interaction_active=result.opening||result.listening||result.thinking||result.speaking;return result;}
}
int gpio_get_level(int pin) { return (pin == 1 ? left_down : right_down) ? 0 : 1; }
'''
state = r'''
bool g_assistant_start_requested = false;
LocalMenuState g_menu;
LocalButtonState g_local_left, g_local_right;
std::mutex g_menu_mutex;
std::atomic<bool> g_provisioning_requested{false}, g_provisioning_cancel_requested{false};
bool g_provisioning_observed = false, g_pairing_retry = false;
'''
tests = r'''
void tick(uint32_t ms = 1) { clock_ms += ms; poll_local_buttons(); }
void click(bool confirm) {
  (!confirm ? right_down : left_down) = true; tick(); tick(31);
  (!confirm ? right_down : left_down) = false; tick(); tick(31);
}
void reset(uint32_t start = 0) {
  mode_changes=0; transfer={}; assistant_starts=assistant_cancels=0;
  lunaredu::pet::conversation::assistant = {};
  g_assistant_start_requested = false;
  product::product_navigation_input(product::Input::Home);
  product::product_navigation_input(product::Input::Confirm);
  for (int i=0; i<10; ++i) product::product_navigation_input(product::Input::Up);
  product::product_navigation_input(product::Input::Home);
  pairing = {}; unbind_state = DeviceLinkUnbindState::Idle; unbind_requests = pairing_requests = 0;
  g_runtime.playlist = {{"a.mp3","/a"},{"b.mp3","/b"},{"c.mp3","/c"},{"d.mp3","/d"}};
  g_runtime.current_index=1; g_runtime.playlist_revision=1; g_runtime.uploading=false; g_runtime.audio_ready=true;
  queued_index=-1; queued_revision=0;
  g_menu = {}; g_local_left = {}; g_local_right = {}; net = {};
  g_provisioning_requested = false; g_provisioning_cancel_requested = false;
  g_provisioning_observed = false; g_pairing_retry = false;
  left_down = right_down = false; tracks = 0; clock_ms = start;
}
// Legacy safety cases start at their existing parent, independently of the
// new physical shortcut. Full product navigation is covered below.
void open_menu() { g_menu.open = true; }
void hold(bool right, uint32_t ms) {
  (right ? right_down : left_down)=true; tick(); tick(31); tick(ms);
}
void release(bool right) {
  (right ? right_down : left_down)=false; tick(); tick(31);
}
void launcher(unsigned index) {
  product::product_navigation_input(product::Input::Confirm);
  g_menu.open=g_menu.product_shell=true;
  for (unsigned i=0; i<index; ++i) route_local_encoder(1);
}
void enter_setup() {
  open_menu(); click(true);
  assert(g_menu.network && !g_provisioning_requested);
  click(true); assert(g_menu.network && !g_provisioning_requested);
  route_local_encoder(-1); click(true);
  assert(!g_menu.open && g_menu.provisioning && g_provisioning_requested);
}
int main() {
  reset();
  (void)product::product_navigation_input(product::Input::Home);
  (void)product::product_navigation_input(product::Input::Confirm);
  g_menu.product_shell = true;
  route_local_encoder(1); click(true); // Assistant
  route_local_encoder(1); click(true); // Pet actions
  assert(product::product_navigation_snapshot().page == product::Page::PetActions);
  click(true);
  assert(requested_pet_action == -1 && g_menu.product_shell);
  catalog_ready = true;
  route_local_encoder(-1); click(true); // Wrap to the last animation.
  assert(requested_pet_action == int(product::kPetClipCount) - 1 && tracks == 0 &&
         !g_menu.product_shell && product::product_navigation_snapshot().page == product::Page::Desktop);
  catalog_ready = false;
  for (uint32_t start : {0U, 0xfffffff0U}) {
    reset(start); click(true); click(false); assert(tracks==2 && mode_changes==0);
    reset(start); hold(false,799); assert(assistant_starts==0 && tracks==0);
    assert(g_menu.hold_percent==0 && g_menu.mode_hold_percent==0);
    tick(1); assert(assistant_starts==1); tick(3000); assert(assistant_starts==1);
    release(false); assert(tracks==0 && mode_changes==0);
    reset(start); hold(false,799); release(false); assert(tracks==1 && assistant_starts==0);
    reset(start); left_down=right_down=true; tick(); tick(31); tick(1000);
    left_down=right_down=false; tick(); tick(31); assert(tracks==0 && assistant_starts==0 && assistant_cancels==0);
    reset(start); launcher(4); click(true); route_local_encoder(1); click(true);
    const auto board=product::product_state_snapshot(0,0).game_board;
    route_local_encoder(1); click(true); // 2048 does not react to physical moves.
    assert(product::product_state_snapshot(0,0).game_board==board);
    click(false); assert(product::product_navigation_snapshot().page==product::Page::ModuleMenu);
    hold(true,800); assert(product::product_navigation_snapshot().page==product::Page::Desktop && !g_menu.open);
    release(true); assert(tracks==0 && assistant_cancels==1);
    // A new press on home can switch track after the consumed hold releases.
    click(false); assert(tracks==1);
    // Global assistant commands preserve the original page, including thinking.
    reset(start); launcher(2); click(true); const auto source=product::product_navigation_snapshot();
    for (int phase=0; phase<4; ++phase) {
      auto &voice=lunaredu::pet::conversation::assistant;
      voice={}; voice.opening=phase==0;voice.listening=phase==1;voice.thinking=phase==2;voice.speaking=phase==3;
      click(false); assert(product::product_navigation_snapshot().revision==source.revision);
      assert(route_local_encoder(1));
    }
    assert(assistant_cancels==4 && tracks==0);
    hold(false,800); release(false); assert(assistant_starts==1);
    // A key pressed before an asynchronous wake cannot act on the new owner.
    reset(start); left_down=true;tick();tick(31);
    lunaredu::pet::conversation::assistant.listening=true;
    release(false);assert(tracks==0 && assistant_starts==0);
  }
  // Settings children go back to Settings, then the launcher, then home.
  reset(); launcher(6);click(true);assert(product::product_navigation_snapshot().page==product::Page::Settings);
  click(true);assert(g_menu.network && g_menu.settings_child);
  click(false);assert(g_menu.product_shell && product::product_navigation_snapshot().page==product::Page::Settings);
  click(false);assert(product::product_navigation_snapshot().page==product::Page::ModuleList);
  click(false);assert(!g_menu.open && product::product_navigation_snapshot().page==product::Page::Desktop);
  launcher(0);assert(product::product_navigation_snapshot().item==6); // Selection survives home.
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
  click(false); assert(!g_menu.information && g_menu.open && g_menu.selected == 2);
  click(false); assert(!g_menu.open && tracks == 0);

  // Network status entry never starts/restarts provisioning by itself.
  for (auto link : {NetworkLinkState::Up, NetworkLinkState::Down, NetworkLinkState::Connecting}) {
    reset(); net.link=link; open_menu(); click(true);
    assert(g_menu.network && !g_provisioning_requested && !g_menu.provisioning);
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
    reset(start); enter_setup();
    // Cancel before the network executor has even started the AP.
    click(false);
    assert(g_menu.network && !g_menu.provisioning);
    assert(!g_provisioning_requested && g_provisioning_cancel_requested && tracks == 0);
    tick(); click(false); click(false); assert(!g_menu.open && tracks == 0);
  }
  for (auto phase : {NetworkProvisioningState::Active, NetworkProvisioningState::Applying}) {
    reset(); enter_setup(); g_provisioning_requested = false;
    net.provisioning = phase; net.link = NetworkLinkState::Down; tick();
    click(true); // Confirm remains inert on the provisioning screen.
    click(false);
    assert(g_menu.network && g_menu.provisioning_dismissed && !g_menu.provisioning);
    click(false); click(false); assert(!g_menu.open && tracks == 0); // Exit again while stop is pending.
    net.provisioning = NetworkProvisioningState::Idle;
    net.link = NetworkLinkState::Connecting; tick();
    assert(!g_menu.provisioning && !g_menu.provisioning_dismissed);
    click(true); assert(tracks == 1); // Normal controls return before association finishes.
  }
  reset(); enter_setup(); g_provisioning_requested = false;
  net.provisioning = NetworkProvisioningState::Active; net.link = NetworkLinkState::Down; tick();
  net.provisioning = NetworkProvisioningState::Idle; tick(); // Failed connection can still be canceled.
  assert(g_menu.provisioning); click(false); assert(g_menu.network && !g_menu.provisioning);
  reset(); enter_setup(); g_provisioning_requested = false;
  net.provisioning = NetworkProvisioningState::Applying; net.link = NetworkLinkState::Connecting; tick();
  net.provisioning = NetworkProvisioningState::Idle; net.link = NetworkLinkState::Up; tick();
  assert(!g_menu.provisioning); click(false); assert(tracks == 1);
  reset(); net.provisioning = NetworkProvisioningState::Active; net.link = NetworkLinkState::Down;
  click(false); assert(g_menu.network && g_provisioning_cancel_requested); // Boot-time automatic AP.
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-menu-') as temp:
    root = Path(temp)
    test = root / 'test.cpp'
    test.write_text(prefix + menu + button + state + encoder + routine + tests, encoding='utf-8')
    binary = root / 'test.exe'
    subprocess.run(['g++', '-std=c++17','-D_WIN32_WINNT=0x0601','-include',str(app/'lunaros_apps/lunaredu_pet/test/host/mutex.h'), '-O2', '-Wall', '-Wextra', '-Werror', '-I'+str(app/'main/product'), '-I'+str(app/'lunaros/include'), '-I'+str(app/'managed_components/bblanchon__arduinojson/src'), str(app/'test/host/product_time_storage.cpp'), str(test), str(app/'main/product/product_state.cpp'), str(app/'main/product/product_state_runtime.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Local menu: pending/active/applying/failed setup cancel, return, success, auto-AP and clock-wrap passed')
