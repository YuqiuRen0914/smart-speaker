"""Run the production avatar loader against controlled account/cache changes."""
from pathlib import Path
import re, subprocess, tempfile
app = Path(__file__).resolve().parents[1]
source = (app / 'main/smart_speaker/smart_speaker_avatar.cpp').read_text(encoding='utf-8')
source = re.sub(r'^#include.*\n', '', source, flags=re.M)
prefix = r'''
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <cassert>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LV_IMAGE_HEADER_MAGIC 25
#define LV_COLOR_FORMAT_RGB888 7
void *heap_caps_malloc(size_t n, int) { return std::malloc(n); }
struct lv_image_dsc_t {
  struct {int magic=0, cf=0, w=0, h=0, stride=0;} header;
  size_t data_size=0; const uint8_t *data=nullptr;
};
namespace lunaredu::lunaros {
enum class DeviceLinkBindingState { Unbound, Bound };
struct DeviceLinkPairingSnapshot {
 DeviceLinkBindingState state=DeviceLinkBindingState::Bound;
 std::string account_id="A", account_avatar_resource_revision="v1", account_avatar_resource_url="/avatar";
 int binding_epoch=1;
 bool account_avatar_resource_available=true;
 int account_avatar_resource_width=96, account_avatar_resource_height=96;
 std::string account_avatar_resource_format="rgb666-u8x3";
} current;
enum class ResourceState { Missing, Error, Ready, Downloading };
enum class ResourceScope { AccountPrivate };
struct ResourceDescriptor {
 std::string id, revision, url, format; int width=0,height=0; size_t maximum_bytes=0;
 ResourceScope scope{};
};
struct ResourceSnapshot {
 ResourceState state=ResourceState::Ready; size_t size_bytes=96*96*3; std::string revision="v1";
} cache;
bool unbind_during_read=false, fail_read=false;
int requests=0;
DeviceLinkPairingSnapshot device_link_pairing_snapshot() { return current; }
ResourceSnapshot resource_snapshot(const std::string &) { return cache; }
bool request_resource(ResourceDescriptor d, int) {
 assert(d.maximum_bytes==96*96*3 && d.scope==ResourceScope::AccountPrivate); ++requests; return true;
}
bool read_ready_resource_into(const std::string &, uint8_t *p,size_t n,size_t *size,ResourceSnapshot *snapshot) {
 std::memset(p,0x58,n); *size=n; *snapshot=cache;
 if(unbind_during_read) current.state=DeviceLinkBindingState::Unbound;
 return !fail_read;
}
}
namespace lunaredu::account_resources {
using AccountResourcesSnapshot = lunaros::DeviceLinkPairingSnapshot;
AccountResourcesSnapshot snapshot() { return lunaros::current; }
}
'''
tests = r'''
int main() {
 using namespace smart_speaker;
 using namespace lunaredu::lunaros;
 update_account_avatar(current);
 assert(account_avatar_image() && account_avatar_image()->data[0]==0x58);
 const auto generation=account_avatar_generation();
 cache.state=ResourceState::Missing; // Offline keeps the same account's already loaded bitmap.
 update_account_avatar(current); assert(account_avatar_image() && account_avatar_generation()==generation);
 current.account_avatar_resource_revision="v2";
 update_account_avatar(current); assert(requests==1 && account_avatar_image());
 cache={ResourceState::Ready, 100, "v2"}; // A truncated response cannot replace the old avatar.
 update_account_avatar(current); assert(account_avatar_generation()==generation);
 cache={ResourceState::Ready,96*96*3,"v2"};
 unbind_during_read=true;
 update_account_avatar(current); assert(!account_avatar_image());
 unbind_during_read=false;
 current.state=DeviceLinkBindingState::Bound; current.account_id="B"; current.binding_epoch=2;
 cache.state=ResourceState::Downloading;
 update_account_avatar(current); assert(!account_avatar_image());
 cache.state=ResourceState::Ready;
 update_account_avatar(current); assert(account_avatar_image());
 current.account_avatar_resource_available=false; // Explicit avatar removal.
 update_account_avatar(current); assert(!account_avatar_image());
 current.account_avatar_resource_available=true; current.account_avatar_resource_revision="v3";
 cache.revision="v3"; fail_read=true;
 update_account_avatar(current); assert(!account_avatar_image());
 fail_read=false; update_account_avatar(current); assert(account_avatar_image());
 current.state=DeviceLinkBindingState::Unbound;
 update_account_avatar(current); assert(!account_avatar_image());
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-avatar-') as temp:
 p=Path(temp);(p/'test.cpp').write_text(prefix+source+tests,encoding='utf-8')
 subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
print('Avatar: cache reuse, pending revision, truncated/read failure, account change, removal and concurrent unbind passed')
