#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lunaredu_lunaros/delivery/resource_contract.h"

namespace lunaredu::lunaros {

struct ServiceNode;

inline constexpr size_t kResourceDownloadQueueCapacity = 12;
inline constexpr size_t kResourceRegistryCapacity = 32;

struct ResourceTransportConfig {
  std::string base_url;
  std::string device_serial;
  std::string bearer_token;
  bool allow_plaintext_development = false;
};

ServiceNode resource_service_node();
void configure_resource_transport(ResourceTransportConfig config);
void set_private_resource_scope(uint32_t binding_epoch,
                                const std::string &account_id);
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

}  // namespace lunaredu::lunaros
