#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

enum class ResourceState : uint8_t {
  Missing,
  Queued,
  Downloading,
  Ready,
  Error,
};

enum class ResourceScope : uint8_t {
  Public,
  AccountPrivate,
  ProjectPrivate,
};

struct ResourceDescriptor {
  std::string id;
  std::string revision;
  std::string url;
  std::string sha256;
  std::string format;
  uint16_t width = 0;
  uint16_t height = 0;
  size_t maximum_bytes = 512 * 1024;
  ResourceScope scope = ResourceScope::Public;
};

struct ResourceSnapshot {
  ResourceState state = ResourceState::Missing;
  std::string id;
  std::string revision;
  std::string path;
  std::string error;
  std::string format;
  std::string sha256;
  uint16_t width = 0;
  uint16_t height = 0;
  size_t size_bytes = 0;
  int progress = 0;
  // Identity of this publication within the current boot, including scope
  // changes and retries of an otherwise identical descriptor.
  uint64_t generation = 0;
};

} // namespace lunaredu::lunaros
