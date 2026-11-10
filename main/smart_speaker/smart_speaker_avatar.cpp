#include "smart_speaker_avatar.h"
#include "lunaredu_account_resources.h"
#include "lunaredu_lunaros/delivery/resources.h"
#include <cstring>
#include <esp_heap_caps.h>

namespace smart_speaker {
namespace {
constexpr size_t kBytes = 96 * 96 * 3;
uint8_t *g_pixels = nullptr;
lv_image_dsc_t g_image{};
std::string g_owner, g_loaded_revision;
int g_epoch = -1;
bool g_ready = false;
uint32_t g_generation = 0;
void clear_avatar() {
  if (g_ready) ++g_generation;
  g_ready = false;
  g_loaded_revision.clear();
  if (g_pixels) std::memset(g_pixels, 0, kBytes);
}
}

void update_account_avatar(const lunaredu::account_resources::AccountResourcesSnapshot &p) {
  using namespace lunaredu::lunaros;
  const bool bound = p.state == DeviceLinkBindingState::Bound && !p.account_id.empty();
  if (!bound || p.account_id != g_owner || p.binding_epoch != g_epoch) {
    clear_avatar();
    g_owner = bound ? p.account_id : "";
    g_epoch = p.binding_epoch;
  }
  if (!bound) return; // LunarOS revokes private on-disk resources on unbind.
  if (!p.account_avatar_resource_available) { clear_avatar(); return; }
  if (p.account_avatar_resource_width != 96 || p.account_avatar_resource_height != 96 ||
      p.account_avatar_resource_format != "rgb666-u8x3" ||
      p.account_avatar_resource_revision.empty() || p.account_avatar_resource_url.empty()) return;
  if (g_ready && g_loaded_revision == p.account_avatar_resource_revision) return;
  const auto resource = resource_snapshot("account-avatar");
  if (resource.state == ResourceState::Missing || resource.state == ResourceState::Error ||
      resource.revision != p.account_avatar_resource_revision) {
    ResourceDescriptor request;
    request.id = "account-avatar";
    request.revision = p.account_avatar_resource_revision;
    request.url = p.account_avatar_resource_url;
    request.format = "rgb666-u8x3";
    request.width = request.height = 96;
    request.maximum_bytes = kBytes;
    request.scope = ResourceScope::AccountPrivate;
    (void)request_resource(std::move(request), 20);
    return;
  }
  if (resource.state != ResourceState::Ready || resource.size_bytes != kBytes) return;
  if (!g_pixels) g_pixels = static_cast<uint8_t *>(heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_pixels) return;
  size_t bytes = 0;
  ResourceSnapshot read;
  if (!read_ready_resource_into("account-avatar", g_pixels, kBytes, &bytes, &read) ||
      bytes != kBytes || read.revision != p.account_avatar_resource_revision) {
    clear_avatar();
    return;
  }
  // Recheck ownership after the cache read; an in-flight unbind must not
  // publish the previous account's picture.
  const auto current = lunaredu::account_resources::snapshot();
  if (current.state != DeviceLinkBindingState::Bound || current.account_id != g_owner ||
      current.binding_epoch != g_epoch || !current.account_avatar_resource_available ||
      current.account_avatar_resource_revision != read.revision) {
    clear_avatar();
    return;
  }
  g_image.header.magic = LV_IMAGE_HEADER_MAGIC;
  g_image.header.cf = LV_COLOR_FORMAT_RGB888;
  g_image.header.w = g_image.header.h = 96;
  g_image.header.stride = 96 * 3;
  g_image.data_size = kBytes;
  g_image.data = g_pixels;
  g_loaded_revision = read.revision;
  g_ready = true;
  ++g_generation;
}

const lv_image_dsc_t *account_avatar_image() { return g_ready ? &g_image : nullptr; }
uint32_t account_avatar_generation() { return g_generation; }
}
