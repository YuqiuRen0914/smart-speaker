#pragma once
#include <cstddef>
#include <cstdint>
#include "app/package/application_profile.h"
#include "app/package/save_format_limits.h"
namespace lunaredu::lunaros::apps {
inline constexpr size_t kMaximumApplications = kApplicationBudget.applications;
inline constexpr size_t kMaximumCatalogBytes = kApplicationBudget.catalog_bytes;
inline constexpr size_t kMaximumPackageBytes = kApplicationBudget.package_bytes;
inline constexpr size_t kMaximumSaveBytes = kApplicationBudget.save_bytes;
inline constexpr size_t kMaximumSaveFileBytes = save_file_maximum_bytes(kMaximumSaveBytes);
inline constexpr uint32_t kApplicationMemoryPages = kApplicationBudget.wasm_memory_pages;
} // namespace lunaredu::lunaros::apps
