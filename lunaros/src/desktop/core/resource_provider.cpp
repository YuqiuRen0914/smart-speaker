#include "lunaredu_lunaros/desktop/core/resource_provider.h"

#include <utility>

namespace lunaredu::desktop {
namespace {

ResourceProvider g_provider;

lunaredu::lunaros::ResourceSnapshot unavailable_snapshot(const std::string &id) {
  lunaredu::lunaros::ResourceSnapshot snapshot;
  snapshot.state = lunaredu::lunaros::ResourceState::Error;
  snapshot.id = id;
  snapshot.error = "desktop resource provider is not configured";
  return snapshot;
}

} // namespace

void configure_resource_provider(ResourceProvider provider) {
  g_provider = std::move(provider);
}

bool request_resource(lunaredu::lunaros::ResourceDescriptor descriptor, int priority) {
  return g_provider.request != nullptr &&
         g_provider.request(g_provider.context, std::move(descriptor), priority);
}

lunaredu::lunaros::ResourceSnapshot resource_snapshot(const std::string &id) {
  if (g_provider.snapshot == nullptr) {
    return unavailable_snapshot(id);
  }
  return g_provider.snapshot(g_provider.context, id);
}

bool read_ready_resource_into(const std::string &id, uint8_t *content,
                              size_t capacity, size_t *content_size,
                              lunaredu::lunaros::ResourceSnapshot *snapshot) {
  if (g_provider.read_into == nullptr) {
    if (snapshot != nullptr) {
      *snapshot = unavailable_snapshot(id);
    }
    return false;
  }
  return g_provider.read_into(g_provider.context, id, content, capacity,
                              content_size, snapshot);
}

bool release_resource(const std::string &id, bool delete_cached) {
  return g_provider.release != nullptr &&
         g_provider.release(g_provider.context, id, delete_cached);
}

void retry_resource(const std::string &id) {
  if (g_provider.retry != nullptr) {
    g_provider.retry(g_provider.context, id);
  }
}

bool resource_publication_current(const std::string &id, uint64_t generation) {
  if (g_provider.is_current != nullptr)
    return g_provider.is_current(g_provider.context, id, generation);
  const auto snapshot = resource_snapshot(id);
  return snapshot.state == lunaredu::lunaros::ResourceState::Ready && snapshot.generation == generation;
}

bool schedule_resource_read(ResourceReadOperation &operation) {
  if (operation.state.load(std::memory_order_acquire) != ResourceReadState::Idle ||
      operation.content == nullptr || operation.capacity == 0 ||
      operation.id.empty() || g_provider.schedule_read == nullptr) return false;
  operation.size = 0;
  operation.generation = 0;
  operation.state.store(ResourceReadState::Pending, std::memory_order_release);
  if (g_provider.schedule_read(g_provider.context, operation)) return true;
  operation.state.store(ResourceReadState::Idle, std::memory_order_release);
  return false;
}

} // namespace lunaredu::desktop
