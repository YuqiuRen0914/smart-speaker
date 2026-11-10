#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "delivery/resource/resource_policy.h"
#include "delivery/resource/resource_registry.h"

namespace lunaredu::lunaros::internal {

struct ResourceRuntimeState {
  explicit ResourceRuntimeState(ResourceDeliveryRegistry::Entries &entries)
      : registry(entries) {}
  std::atomic<bool> started{false};
  std::mutex mutex;
  // Serializes scope transitions with garbage collection, without holding
  // the registry mutex while taking the filesystem mutex.
  std::mutex scope_mutex;
  ResourceDeliveryRegistry registry;
  ResourceTransportConfig transport;
  uint32_t binding_epoch = 0;
  uint32_t scope_generation = 0;
  uint64_t next_resource_generation = 1;
  std::string account_id;
  bool cleanup_pending = false;
  NetworkJobHandle cleanup_job;
  uint16_t cleanup_attempts = 0;
};

ResourceRuntimeState &resource_runtime_state();
uint32_t resource_now_ms();

// Caller holds ResourceRuntimeState::mutex. Fixed registry slots keep
// ResourceEntry addresses stable while the netio runner owns the pointer.
bool queue_resource_download_locked(ResourceEntry &entry,
                                    std::string &error);
// Caller holds runtime.mutex; cleanup uses the existing I/O scheduler.
void queue_private_resource_cleanup_locked();
bool remove_private_resource_files();

} // namespace lunaredu::lunaros::internal
