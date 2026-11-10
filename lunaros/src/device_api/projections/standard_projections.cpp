#include "lunaredu_lunaros/device_api/standard_projections.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include <cstring>

namespace lunaredu::lunaros {
bool install_standard_device_projections(const StandardProjectionContribution *entries,
                                         size_t count, std::string &error) {
  error.clear();
  if (!entries || !count) { error = "standard_projections_empty"; return false; }
  for (size_t index = 0; index < count; ++index) {
    const auto &entry = entries[index];
    if (!entry.id || !*entry.id || !entry.install) {
      error = "invalid_standard_projection"; return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (std::strcmp(entries[previous].id, entry.id) == 0) {
        error = "duplicate_standard_projection:" + std::string(entry.id); return false;
      }
    }
  }
  for (size_t index = 0; index < count; ++index) {
    std::string detail;
    if (!entries[index].install(detail)) {
      error = "standard_projection_failed:" + std::string(entries[index].id) + ":" + detail;
      setup_device_projection_registry();
      return false;
    }
  }
  return true;
}
} // namespace lunaredu::lunaros
