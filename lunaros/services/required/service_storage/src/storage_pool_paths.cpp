#include "lunaros_storage_pool.h"

namespace lunaredu::lunaros {
bool storage_pool_managed_path(const std::string &path) {
  return path == "/managed" || path.rfind("/managed/", 0) == 0;
}
}
