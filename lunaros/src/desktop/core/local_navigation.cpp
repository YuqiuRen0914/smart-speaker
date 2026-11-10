#include "desktop/core/local_navigation_internal.h"
#include "lunaredu_lunaros/desktop/core/local_navigation.h"
#include <cstring>
#include <mutex>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

namespace lunaredu::desktop {
namespace {
std::mutex g_mutex;
LocalPageRequest g_request;
bool g_pending = false;
}
bool request_lunaros_ui_page(const char *page_id) {
#if defined(ESP_PLATFORM) && !CONFIG_LUNAREDU_DESKTOP_LOCAL
  (void)page_id;
  return false;
#else
  if (!page_id || !*page_id) return false;
  size_t length = 0;
  while (length < g_request.page_id.size() && page_id[length]) ++length;
  if (length == g_request.page_id.size()) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  std::memcpy(g_request.page_id.data(), page_id, length + 1);
  ++g_request.revision;
  g_pending = true;
  return true;
#endif
}
bool take_local_page_request(LocalPageRequest &request) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_pending) return false;
  request = g_request;
  g_pending = false;
  return true;
}
}
