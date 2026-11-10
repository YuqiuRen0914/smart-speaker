#pragma once

#include <cstdint>
#include <string>

namespace lunaredu::desktop {
struct PresentationViewport {
  uint16_t width = 480;
  uint16_t height = 320;
};

struct PresentationPage {
  std::string id;
  uint32_t revision = 0;
};

void set_presentation_viewport(uint16_t width, uint16_t height);
PresentationViewport presentation_viewport();
void publish_presentation_page(const char *page_id);
PresentationPage presentation_page();
} // namespace lunaredu::desktop
