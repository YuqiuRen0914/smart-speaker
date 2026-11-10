#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <string>

#include "lunaredu_lunaros/delivery/resource_contract.h"

namespace lunaredu::desktop {


enum class ResourceReadState : uint8_t { Idle, Pending, Succeeded, Failed };

// Caller-owned stable storage. After admission, fields and the destination
// must remain alive and untouched until a terminal state is acquired. The
// producer publishes size before the release-store of its completion state.
struct ResourceReadOperation {
  std::atomic<ResourceReadState> state{ResourceReadState::Idle};
  std::string id;
  std::string revision;
  uint8_t *content = nullptr;
  size_t capacity = 0;
  size_t size = 0;
  uint64_t generation = 0;
};

struct ResourceProvider {
  void *context = nullptr;
  bool (*request)(void *context, lunaredu::lunaros::ResourceDescriptor descriptor,
                  int priority) = nullptr;
  lunaredu::lunaros::ResourceSnapshot (*snapshot)(void *context, const std::string &id) = nullptr;
  bool (*read_into)(void *context, const std::string &id, uint8_t *content,
                    size_t capacity, size_t *content_size,
                    lunaredu::lunaros::ResourceSnapshot *snapshot) = nullptr;
  bool (*release)(void *context, const std::string &id,
                  bool delete_cached) = nullptr;
  void (*retry)(void *context, const std::string &id) = nullptr;
  bool (*schedule_read)(void *context, ResourceReadOperation &operation) = nullptr;
  bool (*is_current)(void *context, const std::string &id, uint64_t generation) = nullptr;
};

void configure_resource_provider(ResourceProvider provider);
bool request_resource(lunaredu::lunaros::ResourceDescriptor descriptor, int priority = 0);
lunaredu::lunaros::ResourceSnapshot resource_snapshot(const std::string &id);
bool read_ready_resource_into(const std::string &id, uint8_t *content,
                              size_t capacity, size_t *content_size = nullptr,
                              lunaredu::lunaros::ResourceSnapshot *snapshot = nullptr);
bool release_resource(const std::string &id, bool delete_cached = false);
void retry_resource(const std::string &id);
bool schedule_resource_read(ResourceReadOperation &operation);
bool resource_publication_current(const std::string &id, uint64_t generation);

} // namespace lunaredu::desktop
