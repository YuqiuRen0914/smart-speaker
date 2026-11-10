#include "device_api/http/http_status.h"
#include <cassert>
#include <initializer_list>
#include <cstdlib>
#include <cstring>
int main() {
  using lunaredu::lunaros::internal::http_status_text;
  // Include conflict from App/OTA and timeout from the retryable error contract.
  for (int code : {200, 201, 202, 204, 302, 400, 401, 403, 404, 405,
                   408, 409, 413, 423, 429, 500, 503}) {
    assert(std::atoi(http_status_text(code)) == code);
  }
  assert(std::strcmp(http_status_text(409), "409 Conflict") == 0);
  assert(std::atoi(http_status_text(-1)) == 500);
}
