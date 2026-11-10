#include <cassert>
#include <iostream>

#include "delivery/resource/resource_download.h"
#include "delivery/resource/resource_policy.h"

using namespace lunaredu::lunaros;

namespace {

ResourceDescriptor valid_descriptor() {
  return {"pet.avatar:idle", "v1", "https://example.com/avatar.rgb666", "",
          "rgb666-u8x3", 10, 10, 300, ResourceScope::Public};
}

void test_descriptor_contract() {
  ResourceDescriptor descriptor = valid_descriptor();
  assert(internal::valid_resource_descriptor(descriptor));
  descriptor.id = "bad/id";
  assert(!internal::valid_resource_descriptor(descriptor));
  descriptor = valid_descriptor();
  descriptor.maximum_bytes = internal::kAbsoluteResourceMaximumBytes + 1;
  assert(!internal::valid_resource_descriptor(descriptor));
}

void test_cached_pixel_size_is_bounded_by_descriptor() {
  ResourceDescriptor descriptor = valid_descriptor();
  assert(internal::resource_cached_size_if_valid(descriptor) == 300);
  descriptor.maximum_bytes = 299;
  assert(internal::resource_cached_size_if_valid(descriptor) == 0);
  descriptor.format = "rgb565a8";
  descriptor.maximum_bytes = 300;
  assert(internal::resource_cached_size_if_valid(descriptor) == 0);
  descriptor.format = "rgb888";
  descriptor.maximum_bytes = 300;
  assert(internal::resource_cached_size_if_valid(descriptor) == 0);
  descriptor.format = "argb8888";
  descriptor.maximum_bytes = 400;
  assert(internal::resource_cached_size_if_valid(descriptor) == 0);
  descriptor.format = "rgb666-u8x3";
  descriptor.maximum_bytes = 300;
  assert(internal::resource_cached_size_if_valid(descriptor) == 300);
  descriptor.format = "a8rgb666-u8x4";
  descriptor.maximum_bytes = 400;
  assert(internal::resource_cached_size_if_valid(descriptor) == 400);
  descriptor.maximum_bytes = 399;
  assert(internal::resource_cached_size_if_valid(descriptor) == 0);
  descriptor.format = "png";
  assert(internal::resource_cached_size_if_valid(descriptor) == 0);
}

void test_failure_backoff_is_bounded() {
  assert(internal::resource_failure_backoff_ms(1) == 2000);
  assert(internal::resource_failure_backoff_ms(4) == 16000);
  assert(internal::resource_failure_backoff_ms(6) == 30000);
  assert(internal::resource_failure_backoff_ms(100) == 30000);
}

void test_same_active_revision_is_reused() {
  assert(internal::resource_request_already_handled(
      "v1", ResourceState::Downloading, 0, valid_descriptor(), 1000));
  ResourceDescriptor next = valid_descriptor();
  next.revision = "revision-b";
  assert(!internal::resource_request_already_handled(
      "v1", ResourceState::Downloading, 0, next, 1000));
}

void test_error_retry_window_is_respected() {
  assert(internal::resource_request_already_handled(
      "v1", ResourceState::Error, 2000, valid_descriptor(), 1999));
  assert(!internal::resource_request_already_handled(
      "v1", ResourceState::Error, 2000, valid_descriptor(), 2000));
}

} // namespace

int main() {
  test_descriptor_contract();
  test_cached_pixel_size_is_bounded_by_descriptor();
  test_failure_backoff_is_bounded();
  test_same_active_revision_is_reused();
  test_error_retry_window_is_respected();
  std::cout << "resource policy tests passed: 5\n";
  return 0;
}
