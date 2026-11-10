#pragma once

#include <cstddef>
#include <string>

#include "lunaros_resource.h"
#include "lunaros_storage_private_paths.h"

namespace lunaredu::lunaros::internal {

inline constexpr const char *kResourceCacheRoot = "/resources";
inline constexpr const char *kPublicResourceRoot = "/resources/public";
inline constexpr const char *kPrivateResourceRoot = kStoragePrivateResourceRoot;
inline constexpr size_t kAbsoluteResourceMaximumBytes = 2 * 1024 * 1024;

using ResourceDownloadProgressCallback =
    void (*)(int progress, const void *context);
using ResourceDownloadCancellationCallback = bool (*)(const void *context);

struct ResourceDownloadResult {
  bool success = false;
  std::string path;
  std::string sha256;
  size_t size_bytes = 0;
  std::string error;
};

struct ResourceCacheScope {
  uint32_t binding_epoch = 0;
  std::string account_id;
};

std::string private_resource_cache_root(const ResourceCacheScope &scope);
std::string resource_cache_path(const ResourceDescriptor &descriptor,
                                const ResourceCacheScope &scope = {});

// Executes one blocking network-to-storage transaction. It owns the temporary
// file, stream hash, size enforcement and final install; callers only publish
// the result if their requested revision is still current.
ResourceDownloadResult download_resource_transaction(
    const ResourceDescriptor &descriptor,
    const ResourceTransportConfig &transport,
    ResourceDownloadProgressCallback progress_callback = nullptr,
    const void *progress_context = nullptr,
    ResourceDownloadCancellationCallback cancellation_callback = nullptr,
    const void *cancellation_context = nullptr,
    const ResourceCacheScope &scope = {});

} // namespace lunaredu::lunaros::internal
