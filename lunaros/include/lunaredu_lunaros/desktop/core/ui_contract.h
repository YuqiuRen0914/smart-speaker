#pragma once

#include <cstddef>
#include <cstdint>
#include "lunaredu_lunaros/desktop/core/view_types.h"

namespace lunaredu::desktop {

inline constexpr size_t kLauncherItemCapacity = 12;
inline constexpr size_t kFileEntryCapacity = 16;
inline constexpr size_t kSavedWifiCapacity = 5;
inline constexpr size_t kWifiScanCapacity = 8;

enum class FileKind : uint8_t {
  Folder = LUNAROS_FILE_FOLDER,
  Python = LUNAROS_FILE_PYTHON,
  Performance = LUNAROS_FILE_PERFORMANCE,
  Other = LUNAROS_FILE_OTHER,
};

} // namespace lunaredu::desktop
