#include "lunaredu_lunaros/device_web/portal.h"
#include "device_web/portal.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "sdkconfig.h"
#include <array>
#include <cstring>
#include <utility>
#ifndef CONFIG_LUNAREDU_DEVICE_WEB_MAX_PAGE_BYTES
#define CONFIG_LUNAREDU_DEVICE_WEB_MAX_PAGE_BYTES 65536
#endif
namespace lunaredu::lunaros {
namespace {
extern const uint8_t kPageStart[] asm("_binary_portal_html_start");
extern const uint8_t kPageEnd[] asm("_binary_portal_html_end");
constexpr size_t kMaxPageBytes = CONFIG_LUNAREDU_DEVICE_WEB_MAX_PAGE_BYTES;
struct Override { const uint8_t *data = nullptr; size_t size = 0; std::string path; bool failed = false; };
std::array<Override, 4> overrides;
std::string name, cached;
int cached_index = -1;
bool frozen = false, configured = false;
bool append(const uint8_t *data, size_t size, void *) {
  if (size > kMaxPageBytes - cached.size()) return false;
  cached.append(reinterpret_cast<const char *>(data), size);
  return true;
}
}
bool configure_device_web(const DeviceWebConfig &config, std::string &error) {
  error.clear();
  if (frozen || configured) { error = "device_web_already_configured"; return false; }
  std::string next_name = config.product_name ? config.product_name : "";
  if (next_name.size() > 160) { error = "invalid_web_product_name"; return false; }
  const DeviceWebPage pages[] = {config.home, config.network, config.binding, config.maintenance};
  std::array<Override, 4> next;
  for (size_t i = 0; i < next.size(); ++i) {
    const auto &p = pages[i];
    if ((p.data == nullptr) != (p.size == 0) || p.size > kMaxPageBytes) {
      error = "invalid_web_page"; return false;
    }
    if (p.storage_path && (!storage_is_safe_path(p.storage_path) || p.storage_path[0] != '/' || std::strlen(p.storage_path) > 128)) {
      error = "invalid_web_page_path"; return false;
    }
    next[i] = {p.data, p.size, p.storage_path ? p.storage_path : "", false};
  }
  name = std::move(next_name);
  overrides = std::move(next);
  configured = true;
  return true;
}
namespace device_web {
void freeze() { frozen = true; }
const char *product_name() { return name.c_str(); }
PageView page(Page selected, bool recovery) {
  freeze();
  const auto builtin = PageView{kPageStart, static_cast<size_t>(kPageEnd-kPageStart-1)};
  if (recovery) return builtin;
  const auto index = static_cast<size_t>(selected);
  if (index >= overrides.size()) return builtin;
  auto &asset = overrides[index];
  if (asset.data) return {asset.data, asset.size};
  if (asset.path.empty() || asset.failed) return builtin;
  if (cached_index != static_cast<int>(index)) {
    std::string().swap(cached);
    cached_index = -1;
    std::string error;
    if (!storage_stream_file(asset.path.c_str(), append, nullptr, &error) || cached.empty()) {
      asset.failed = true;
      std::string().swap(cached);
      LOGW("device.web", "product page unavailable; using standard portal: %s", error.c_str());
      return builtin;
    }
    cached_index = static_cast<int>(index);
  }
  return {reinterpret_cast<const uint8_t *>(cached.data()), cached.size()};
}
}
} // namespace lunaredu::lunaros
