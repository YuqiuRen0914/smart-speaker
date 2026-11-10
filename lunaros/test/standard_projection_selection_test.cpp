#include "lunaredu_lunaros/device_api/standard_projections.h"
#include <cassert>
#include <iostream>
#include <vector>

namespace { std::vector<int> installed; bool fail_network = false; }
namespace lunaredu::lunaros {
// No storage/update/identity/system/task installer is linked in this test.
bool setup_service_projection(std::string &) { installed.push_back(1); return true; }
bool setup_network_projections(std::string &error) {
  if (fail_network) { error = "network_failed"; return false; }
  installed.push_back(2); return true;
}
void setup_device_projection_registry() { installed.clear(); }
}
int main() {
  using namespace lunaredu::lunaros;
  std::string error;
  assert((setup_standard_device_projections<standard_projection::services,
      standard_projection::network>(error)));
  assert((installed == std::vector<int>{1, 2}));
  installed.clear(); fail_network = true;
  assert((!setup_standard_device_projections<standard_projection::services,
      standard_projection::network>(error)));
  assert(installed.empty() && error == "standard_projection_failed:network:network_failed");
  fail_network = false;
  assert((!setup_standard_device_projections<standard_projection::services,
      standard_projection::services>(error)));
  assert(installed.empty());
  std::cout << "Explicit projections link without unused installers and roll back failures\n";
}
