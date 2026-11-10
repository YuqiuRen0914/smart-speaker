#pragma once
#include "lunaros_resource_service_id.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

struct ServiceNode;

inline constexpr size_t kResourceDownloadQueueCapacity = 12;
inline constexpr size_t kResourceRegistryCapacity = 32;

struct ResourceTransportConfig {
  std::string base_url;
  std::string device_serial;
  std::string bearer_token;
};

ServiceNode resource_service_node();
void configure_resource_transport(ResourceTransportConfig config);
// The authority owner commits its durable record before applying this runtime
// scope. Suspension revokes private publications and preserves disk caches.
void suspend_private_resource_scope();
bool set_private_resource_scope(uint32_t binding_epoch,
                                const std::string &account_id, std::string &error);
bool request_resource(ResourceDescriptor descriptor, int priority = 0);
// Releases the in-memory delivery slot. Cached content may be retained so a
// later request for the same revision can be satisfied without a download.
bool release_resource(const std::string &id, bool delete_cached = false);
ResourceSnapshot resource_snapshot(const std::string &id);
// Allocation-free validation for consumers holding a previously read buffer.
bool resource_publication_current(const std::string &id, uint64_t generation);
bool read_ready_resource(const std::string &id, std::vector<uint8_t> &content,
                         ResourceSnapshot *snapshot = nullptr);
bool read_ready_resource_into(const std::string &id, uint8_t *content,
                              size_t capacity, size_t *content_size = nullptr,
                              ResourceSnapshot *snapshot = nullptr);
void retry_resource(const std::string &id);

enum class ResourceReadState : uint8_t { Idle, Pending, Succeeded, Failed };

// Caller-owned stable storage. After admission, fields and the destination
// must remain alive and untouched until a terminal state is acquired. The
// producer publishes size before the release-store of its completion state.
// Poll resource_read_state(), not state directly: it also retires cancelled
// queued jobs. Scheduling, polling and resetting Idle belong to one caller.
struct ResourceReadOperation {
  std::atomic<ResourceReadState> state{ResourceReadState::Idle};
  std::string id;
  std::string revision;
  uint8_t *content = nullptr;
  size_t capacity = 0;
  size_t size = 0;
  uint64_t generation = 0;

 private:
  uint32_t job_id_ = 0;
  uint64_t publication_ = 0;
  friend bool schedule_resource_read(ResourceReadOperation &);
  friend ResourceReadState resource_read_state(ResourceReadOperation &);
};

bool schedule_resource_read(ResourceReadOperation &operation);
ResourceReadState resource_read_state(ResourceReadOperation &operation);

}  // namespace lunaredu::lunaros
