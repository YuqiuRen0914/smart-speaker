#include "lunaredu_lunaros/desktop/core/page_state.h"

#include <mutex>

namespace lunaredu::desktop {
namespace {
std::mutex g_mutex;
PresentationViewport g_viewport;
PresentationPage g_page;
} // namespace

void set_presentation_viewport(uint16_t width, uint16_t height) {
  if (!width || !height) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_viewport = {width, height};
}

PresentationViewport presentation_viewport() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_viewport;
}

void publish_presentation_page(const char *page_id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const char *id = page_id ? page_id : "";
  if (g_page.id == id) return;
  g_page.id = id;
  ++g_page.revision;
}

PresentationPage presentation_page() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_page;
}
} // namespace lunaredu::desktop
