#include "desktop/core/local_navigation_internal.h"
#include "lunaredu_lunaros/desktop/core/local_navigation.h"
#include <cassert>
#include <iostream>
#include <string>
int main() {
  using namespace lunaredu::desktop;
  LocalPageRequest result;
  assert(!take_local_page_request(result));
  assert(!request_lunaros_ui_page(nullptr));
  assert(!request_lunaros_ui_page(""));
  assert(!request_lunaros_ui_page(std::string(49, 'x').c_str()));
  assert(request_lunaros_ui_page("weather"));
  assert(request_lunaros_ui_page("settings"));
  assert(take_local_page_request(result));
  assert(std::string(result.page_id.data()) == "settings");
  assert(result.revision == 2);
  assert(!take_local_page_request(result));
  assert(request_lunaros_ui_page(std::string(48, 'x').c_str()));
  assert(take_local_page_request(result));
  assert(std::string(result.page_id.data()).size() == 48);
  std::cout << "Bounded local mirror requests, coalescing and consumption passed\n";
}
