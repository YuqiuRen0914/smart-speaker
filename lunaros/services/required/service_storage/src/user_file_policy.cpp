#include "lunaros_storage.h"
#include "lunaros_storage_user_file_policy.h"
#include <cstring>
namespace lunaredu::lunaros {
bool storage_user_file_path_allowed(const char *path) {
  if(!path || std::strlen(path)>255 || !storage_is_safe_path(path)) return false;
  const auto normalized=storage_normalize_path(path);
  return normalized==path && user_file_directory_allowed(normalized);
}
}
