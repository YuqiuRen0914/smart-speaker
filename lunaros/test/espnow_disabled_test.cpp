#include <cassert>
#include <iostream>
#include "lunaredu_lunaros/network/espnow.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "network/espnow/espnow_internal.h"
using namespace lunaredu::lunaros;
namespace lunaredu::lunaros {
bool invoke_service_operation(const char *, ServiceOperation operation,
                              void *context, std::string &error, uint32_t) {
  ServiceOperationResult result; operation(context, result);
  error = result.error; return result.ok;
}
}
int main() {
  std::string error;
  assert(internal::start_espnow_on_owner({}, error));
  assert(!internal::start_espnow_on_owner({true,1,0}, error));
  assert(error == "espnow_disabled_in_build");
  assert(!espnow_ready() && !espnow_status().initialized);
  assert(!espnow_set_channel(1, error));
  internal::poll_espnow_on_owner();
  internal::stop_espnow_on_owner();
  tick_espnow();
  std::cout << "ESP-NOW disabled configuration tests passed\n";
}
