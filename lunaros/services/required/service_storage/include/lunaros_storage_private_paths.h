#pragma once
#include <string_view>
namespace lunaredu::lunaros {
// Native OS/services own this namespace. Generic file projections must never
// expose or mutate it, even when the caller owns the currently bound device.
// Callers must first canonicalize paths with Storage's public path contract.
inline constexpr std::string_view kStoragePrivateRoot="/_lunaros_private";
inline constexpr const char *kStoragePrivateResourceRoot="/resources/private";
inline bool storage_private_path(std::string_view canonical_path) {
  const auto within=[canonical_path](std::string_view root) {
    return canonical_path==root ||
        (canonical_path.size()>root.size() && canonical_path.substr(0,root.size())==root &&
         canonical_path[root.size()]=='/');
  };
  return within(kStoragePrivateRoot) || within(kStoragePrivateResourceRoot);
}
}
