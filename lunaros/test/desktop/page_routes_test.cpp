#include "page_routes.h"
#include <cassert>
#include <cstring>

int main() {
  for (int value = PAGE_DESKTOP; value <= PAGE_MOTION_TEST; ++value) {
    const auto page = static_cast<lunaros_page_t>(value);
    lunaros_page_t decoded = PAGE_DESKTOP;
    assert(lunaros_page_for_route_id(lunaros_page_route_id(page), &decoded));
    assert(decoded == page);
  }
  lunaros_page_t page = PAGE_SETTINGS;
  assert(!lunaros_page_for_route_id(nullptr, &page));
  assert(!lunaros_page_for_route_id("desktop", nullptr));
  assert(!lunaros_page_for_route_id("", &page));
  assert(!lunaros_page_for_route_id("unknown", &page));
  assert(!lunaros_page_for_route_id("Desktop", &page));
  assert(page == PAGE_SETTINGS);
  assert(std::strcmp(lunaros_page_route_id(static_cast<lunaros_page_t>(999)),
                     "desktop") == 0);
}
