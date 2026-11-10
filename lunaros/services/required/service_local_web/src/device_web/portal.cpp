#include "lunaredu_local_web_portal.h"
#include "device_web/portal_internal.h"
#include "lunaros_storage.h"
#include <esp_log.h>
#include "sdkconfig.h"
#include <array>
#include <cstring>
#include <utility>
#ifndef CONFIG_LUNAREDU_DEVICE_WEB_MAX_PAGE_BYTES
#define CONFIG_LUNAREDU_DEVICE_WEB_MAX_PAGE_BYTES 65536
#endif
namespace lunaredu::lunaros {
extern const uint8_t kPageStart[] asm("_binary_portal_html_start");
extern const uint8_t kPageEnd[] asm("_binary_portal_html_end");
extern const uint8_t kStyleStart[] asm("_binary_portal_css_start");
extern const uint8_t kStyleEnd[] asm("_binary_portal_css_end");
extern const uint8_t kScriptStart[] asm("_binary_portal_js_start");
extern const uint8_t kScriptEnd[] asm("_binary_portal_js_end");
extern const uint8_t kFactoryPageStart[] asm("_binary_factory_html_start");
extern const uint8_t kFactoryPageEnd[] asm("_binary_factory_html_end");
extern const uint8_t kClientStart[] asm("_binary_device_client_js_start");
extern const uint8_t kClientEnd[] asm("_binary_device_client_js_end");
extern const uint8_t kApplicationStyleStart[] asm("_binary_application_css_start");
extern const uint8_t kApplicationStyleEnd[] asm("_binary_application_css_end");
namespace {
constexpr size_t kMaxPageBytes = CONFIG_LUNAREDU_DEVICE_WEB_MAX_PAGE_BYTES;
struct Override { const uint8_t *data = nullptr; size_t size = 0; std::string path; bool failed = false; };
std::array<Override, 4> overrides;
std::string name, application_label, cached;
int cached_index = -1;
bool frozen = false, configured = false;
bool bound_static_pages = false;
bool bound_lan_factory_access = false;
const char *const *local_operations = nullptr;
size_t local_operation_count = 0;
void *operation_gate_context = nullptr;
bool (*operation_gate)(void *, const char *) = nullptr;
bool append(const uint8_t *data, size_t size, void *) {
  if (size > kMaxPageBytes - cached.size()) return false;
  cached.append(reinterpret_cast<const char *>(data), size);
  return true;
}
}
bool configure_device_web(const DeviceWebConfig &config, std::string &error) {
  error.clear();
  if (frozen || configured) { error = "device_web_already_configured"; return false; }
  if (config.local_operation_count > 16 ||
      (config.local_operation_count && !config.local_operations)) {
    error = "invalid_local_operations"; return false;
  }
  for (size_t i = 0; i < config.local_operation_count; ++i) {
    if (!config.local_operations[i] || !config.local_operations[i][0]) {
      error = "invalid_local_operations"; return false;
    }
  }
  std::string next_name = config.product_name ? config.product_name : "";
  if (next_name.size() > 160) { error = "invalid_web_product_name"; return false; }
  std::string next_application_label = config.application_name ? config.application_name : "";
  if (next_application_label.size() > 96) { error = "invalid_web_application_name"; return false; }
  const DeviceWebPage pages[] = {config.application, config.network, config.binding, config.maintenance};
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
  application_label = std::move(next_application_label);
  overrides = std::move(next);
  configured = true;
  bound_static_pages = config.bound_static_pages;
  bound_lan_factory_access = config.bound_lan_factory_access;
  local_operations = config.local_operations;
  local_operation_count = config.local_operation_count;
  operation_gate = config.operation_gate;
  operation_gate_context = config.operation_gate_context;
  return true;
}
bool device_web_bound_static_page_allowed(const char *route) {
  if (!route) return false;
  // Loading the built-in shell never authorizes management operations. Keep
  // it reachable for public/help access, but preserve custom-page policies.
  constexpr const char *public_pages[] = {
      "/portal.css", "/portal.js", "/device-web/config", "/device-web/admin/request", "/server",
      "/recovery", "/version", "/debug", "/", "/application.css", "/device-client.js"};
  for (const char *page : public_pages)
    if (std::strcmp(route, page) == 0) return true;
  const char *standard_pages[] = {"/app", "/setup", "/binding", "/maintenance"};
  for (size_t i = 0; i < overrides.size(); ++i)
    if (std::strcmp(route, standard_pages[i]) == 0 &&
          i < 2 && !overrides[i].data && (overrides[i].path.empty() || overrides[i].failed)) return true;
  if (!bound_static_pages) return false;
  constexpr const char *pages[] = {
      "/app", "/portal.css", "/device-web/config"};
  for (const char *page : pages)
    if (std::strcmp(route, page) == 0) return true;
  return false;
}
bool device_web_bound_factory_page_allowed(const char *route) {
  if (!bound_lan_factory_access || !route) return false;
  constexpr const char *pages[] = {
      "/", "/factory", "/portal.css", "/portal.js",
      "/device-web/config", "/favicon.ico"};
  for (const char *page : pages)
    if (std::strcmp(route, page) == 0) return true;
  return false;
}
bool device_web_bound_factory_operation_allowed(const char *path, bool post) {
  if (!bound_lan_factory_access || !path) return false;
  constexpr const char *read_only[] = {
      "/api/v1/modules/factory_mode/status.get",
      "/api/v1/modules/factory_mode/catalog.get",
      "/api/v1/modules/factory_audio/status.get",
      "/api/v1/modules/assistant/status.get",
      "/api/v1/modules/assistant/tools.get",
      "/api/v1/modules/media/status.get",
      "/api/v1/modules/provisioning/status"};
  constexpr const char *mutating[] = {
      "/api/v1/modules/factory_mode/enter",
      "/api/v1/modules/factory_mode/exit",
      "/api/v1/modules/factory_mode/start",
      "/api/v1/modules/factory_mode/confirm",
      "/api/v1/modules/factory_mode/reset",
      "/api/v1/modules/factory_audio/speaker.start",
      "/api/v1/modules/factory_audio/microphone.start",
      "/api/v1/modules/factory_audio/confirm",
      "/api/v1/modules/assistant/standby_mode.set",
      "/api/v1/modules/assistant/listening.toggle",
      "/api/v1/modules/assistant/conversation.test"};
  if (post) {
    for (const char *operation : mutating)
      if (std::strcmp(path, operation) == 0) return true;
  } else {
    for (const char *operation : read_only)
      if (std::strcmp(path, operation) == 0) return true;
  }
  return false;
}
bool device_web_operation_permitted(const char *operation) {
  return !operation_gate || operation_gate(operation_gate_context, operation);
}
bool device_web_local_operation_allowed(const char *operation) {
  if (!operation) return false;
  for (size_t i = 0; i < local_operation_count; ++i)
    if (std::strcmp(operation, local_operations[i]) == 0) return true;
  return false;
}
namespace device_web {
void freeze() { frozen = true; }
const char *product_name() { return name.c_str(); }
const char *application_name() { return application_label.c_str(); }
bool has_page_override(Page selected) {
  const auto index = static_cast<size_t>(selected);
  if (index >= overrides.size()) return false;
  const auto &value = overrides[index];
  return value.data || (!value.path.empty() && !value.failed);
}
PageView page(Page selected, bool recovery) {
  freeze();
  const auto builtin = PageView{kPageStart, static_cast<size_t>(kPageEnd-kPageStart), true};
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
      ESP_LOGW("device.web", "product page unavailable; using standard portal: %s", error.c_str());
      return builtin;
    }
    cached_index = static_cast<int>(index);
  }
  return {reinterpret_cast<const uint8_t *>(cached.data()), cached.size()};
}
DeviceWebAssetView asset(DeviceWebAsset selected) {
  if (selected == DeviceWebAsset::Client) return {kClientStart, static_cast<size_t>(kClientEnd - kClientStart), true};
  if (selected == DeviceWebAsset::ApplicationStyle) return {kApplicationStyleStart, static_cast<size_t>(kApplicationStyleEnd - kApplicationStyleStart), true};
  if (selected == DeviceWebAsset::Style) return {kStyleStart, static_cast<size_t>(kStyleEnd - kStyleStart), true};
  if (selected == DeviceWebAsset::Script) return {kScriptStart, static_cast<size_t>(kScriptEnd - kScriptStart), true};
  return {kFactoryPageStart, static_cast<size_t>(kFactoryPageEnd - kFactoryPageStart), true};
}
}
} // namespace lunaredu::lunaros
