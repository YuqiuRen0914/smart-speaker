#pragma once

#include <string>

namespace lunaredu::lunaros {

inline bool user_file_directory_allowed(const std::string &path) {
  return path == "/scripts" || path.compare(0, 9, "/scripts/") == 0 ||
         path == "/performances" ||
         path.compare(0, 14, "/performances/") == 0 || path == "/saves" || path.compare(0, 7, "/saves/") == 0;
}

} // namespace lunaredu::lunaros
