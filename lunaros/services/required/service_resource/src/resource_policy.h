#pragma once

#include "lunaros_resource.h"

namespace lunaredu::lunaros::internal {

bool valid_resource_descriptor(const ResourceDescriptor &descriptor);
uint32_t resource_failure_backoff_ms(uint8_t failure_count);
size_t resource_cached_size_if_valid(const ResourceDescriptor &descriptor);
bool resource_request_already_handled(
    const std::string &current_revision, ResourceState current_state,
    uint32_t retry_after_ms, const ResourceDescriptor &descriptor,
    uint32_t now_ms);

} // namespace lunaredu::lunaros::internal
