#include "delivery/resource/resource_policy.h"

#include <algorithm>
#include <cctype>
#include <limits>

#include "delivery/resource/resource_download.h"

namespace lunaredu::lunaros::internal {

bool valid_resource_descriptor(const ResourceDescriptor &descriptor) {
  const std::string &id = descriptor.id;
  const bool valid_id =
      !id.empty() && id.size() <= 160 &&
      std::all_of(id.begin(), id.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' ||
               character == '_' || character == ':' || character == '.';
      });
  return valid_id && !descriptor.revision.empty() &&
         !descriptor.url.empty() && descriptor.width > 0 &&
         descriptor.height > 0 && descriptor.maximum_bytes > 0 &&
         descriptor.maximum_bytes <= kAbsoluteResourceMaximumBytes;
}

uint32_t resource_failure_backoff_ms(uint8_t failure_count) {
  const uint8_t bounded_count = std::min<uint8_t>(failure_count, 6);
  return std::min<uint32_t>(1000U << bounded_count, 30000U);
}

size_t resource_cached_size_if_valid(const ResourceDescriptor &descriptor) {
  const size_t bytes_per_pixel =
      descriptor.format == "rgb666-u8x3" ? 3U
      : descriptor.format == "a8rgb666-u8x4" ? 4U
                                           : 0U;
  if (bytes_per_pixel == 0 || descriptor.width == 0 || descriptor.height == 0) {
    return 0;
  }
  const size_t width = descriptor.width;
  const size_t height = descriptor.height;
  if (width > std::numeric_limits<size_t>::max() / height) {
    return 0;
  }
  const size_t pixels = width * height;
  if (pixels > std::numeric_limits<size_t>::max() / bytes_per_pixel) {
    return 0;
  }
  const size_t expected_size = pixels * bytes_per_pixel;
  return expected_size <= descriptor.maximum_bytes ? expected_size : 0;
}

bool resource_request_already_handled(
    const std::string &current_revision, ResourceState current_state,
    uint32_t retry_after_ms, const ResourceDescriptor &descriptor,
    uint32_t now_ms) {
  if (current_revision != descriptor.revision) {
    return false;
  }
  if (current_state == ResourceState::Ready ||
      current_state == ResourceState::Queued ||
      current_state == ResourceState::Downloading) {
    return true;
  }
  return current_state == ResourceState::Error &&
         static_cast<int32_t>(now_ms - retry_after_ms) < 0;
}

} // namespace lunaredu::lunaros::internal
