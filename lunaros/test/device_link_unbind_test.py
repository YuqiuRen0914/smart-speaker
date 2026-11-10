"""Host-check production unbind admission and completion, without an account."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
scheduler = (root / 'src/device_link/device_link_scheduler.cpp').read_text(encoding='utf-8')
service = (root / 'src/device_link/device_link_service.cpp').read_text(encoding='utf-8')
request = scheduler[scheduler.index('bool request_unbind() {'):scheduler.index('void request_discovery(')]
guard = scheduler[scheduler.index('  struct UnbindCompletion {'):scheduler.index('  // Do this admission check')]
admission = service[service.index('bool request_device_link_unbind() {'):service.index('DeviceLinkPairingSnapshot device_link_pairing_snapshot() {')]
code = r'''
#include <atomic>
#include <cassert>
enum class DeviceLinkUnbindState { Idle, Pending, Succeeded, Failed };
enum class DeviceLinkBindingState { Bound, Unbound, Error };
struct DeviceLinkPairingSnapshot { DeviceLinkBindingState state; } pairing{DeviceLinkBindingState::Bound};
bool online=true;
namespace device_link {
std::atomic<bool> g_unbind_requested{false};
std::atomic<DeviceLinkUnbindState> g_unbind_state{DeviceLinkUnbindState::Idle};
bool network_connected() { return online; }
DeviceLinkPairingSnapshot pairing_snapshot() { return pairing; }
''' + request + r'''
enum class DeviceLinkOperation { Unbind, Presence };
void complete(DeviceLinkOperation operation, bool early_failure, bool success) {
  bool succeeded=false;
''' + guard + r'''
  if (early_failure) return;
  succeeded=success;
}
}
''' + admission + r'''
int main() {
  using namespace device_link;
  online=false;assert(!request_device_link_unbind());assert(!g_unbind_requested);
  online=true;pairing.state=DeviceLinkBindingState::Unbound;
  assert(!request_device_link_unbind());assert(!g_unbind_requested);
  pairing.state=DeviceLinkBindingState::Bound;
  assert(request_device_link_unbind());assert(g_unbind_requested.exchange(false));
  assert(!request_device_link_unbind());assert(!g_unbind_requested);
  // Unrelated presence completion cannot finish the queued logout.
  complete(DeviceLinkOperation::Presence,false,true);
  assert(device_link_unbind_state()==DeviceLinkUnbindState::Pending);
  complete(DeviceLinkOperation::Unbind,true,false);
  assert(device_link_unbind_state()==DeviceLinkUnbindState::Failed);
  assert(request_device_link_unbind());assert(g_unbind_requested.exchange(false));
  complete(DeviceLinkOperation::Unbind,false,false);
  assert(device_link_unbind_state()==DeviceLinkUnbindState::Failed);
  assert(request_device_link_unbind());assert(g_unbind_requested.exchange(false));
  complete(DeviceLinkOperation::Unbind,false,true);
  assert(device_link_unbind_state()==DeviceLinkUnbindState::Succeeded);
  // Presence can replace pairing state without erasing the operation result.
  pairing.state=DeviceLinkBindingState::Unbound;
  complete(DeviceLinkOperation::Presence,false,false);
  assert(device_link_unbind_state()==DeviceLinkUnbindState::Succeeded);
}
'''
with tempfile.TemporaryDirectory(prefix='device-unbind-') as temp:
    source = Path(temp) / 'test.cpp'
    binary = Path(temp) / 'test.exe'
    source.write_text(code, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Unbind: offline/unbound rejection, duplicate suppression, early/HTTP failure and durable success passed')
