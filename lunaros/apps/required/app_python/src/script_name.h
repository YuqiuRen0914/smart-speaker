#pragma once
#include <string>

namespace lunaredu::python::scripts {
// Script IDs are extensionless ASCII identifiers. Reject invalid input instead
// of silently aliasing it to another persisted script.
inline bool valid_script_name(const std::string &name) {
  if (name.empty() || name.size() > 32) return false;
  for (const unsigned char ch : name) {
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
  }
  return true;
}
} // namespace lunaredu::python::scripts
