#include "lunaredu_lunaros/device_api/device_projection.h"

#include <stddef.h>
#include <string.h>

#include "kernel/static_mutex.h"
#include "device_api/projections/device_projection_internal.h"

namespace lunaredu::lunaros {

namespace {

constexpr size_t kMaxContributions = 12;

DeviceMetadataProjection g_contributions[kMaxContributions];
size_t g_contribution_count = 0;
bool g_registry_sealed = false;
lunaredu::lunaros::StaticMutex g_registry_mutex;

void set_error(std::string *error, const char *value) {
  if (error != nullptr) {
    *error = value;
  }
}

} // namespace

void setup_device_metadata_index() {
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  for (auto &contribution : g_contributions) {
    contribution = {};
  }
  g_contribution_count = 0;
  g_registry_sealed = false;
}

bool register_device_metadata_index(
    const DeviceMetadataProjection &contribution, std::string *error,
    bool (*commit)(void *, std::string *), void *context) {
  if (error != nullptr) {
    error->clear();
  }
  if (contribution.id == nullptr || contribution.id[0] == '\0' ||
      (contribution.status_provider == nullptr &&
       contribution.info_provider == nullptr) ||
      (contribution.status_provider != nullptr &&
       (contribution.status_section == nullptr ||
        contribution.status_section[0] == '\0')) ||
      (contribution.status_provider == nullptr &&
       contribution.status_section != nullptr)) {
    set_error(error, "invalid_device_metadata_contribution");
    return false;
  }

  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  for (size_t index = 0; index < g_contribution_count; ++index) {
    const DeviceMetadataProjection &registered = g_contributions[index];
    if (strcmp(registered.id, contribution.id) == 0) {
      const bool same = registered.status_section == contribution.status_section &&
                        registered.status_provider == contribution.status_provider &&
                        registered.info_provider == contribution.info_provider;
      if (!same) {
        set_error(error, "device_metadata_id_conflict");
      }
      return same && (commit == nullptr || commit(context, error));
    }
    if (contribution.status_provider != nullptr &&
        registered.status_section != nullptr &&
        strcmp(registered.status_section, contribution.status_section) == 0) {
      set_error(error, "device_metadata_status_section_conflict");
      return false;
    }
  }
  if (g_registry_sealed) {
    set_error(error, "device_metadata_index_sealed");
    return false;
  }
  if (g_contribution_count >= kMaxContributions) {
    set_error(error, "device_metadata_registry_full");
    return false;
  }
  if (commit != nullptr && !commit(context, error)) return false;
  g_contributions[g_contribution_count++] = contribution;
  return true;
}

bool seal_device_metadata_index(std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  if (g_contribution_count == 0) {
    set_error(error, "device_metadata_registry_empty");
    return false;
  }
  g_registry_sealed = true;
  return true;
}

bool device_metadata_index_sealed() {
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  return g_registry_sealed;
}

void fill_device_status_sections(JsonObject payload) {
  DeviceMetadataProjection snapshot[kMaxContributions];
  size_t count = 0;
  {
    lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
    count = g_contribution_count;
    for (size_t index = 0; index < count; ++index) {
      snapshot[index] = g_contributions[index];
    }
  }
  for (size_t index = 0; index < count; ++index) {
    const auto &contribution = snapshot[index];
    if (contribution.status_provider != nullptr) {
      contribution.status_provider(
          payload[contribution.status_section].to<JsonObject>());
    }
  }
}

void fill_device_info_payload(JsonObject payload) {
  DeviceMetadataProjection snapshot[kMaxContributions];
  size_t count = 0;
  {
    lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
    count = g_contribution_count;
    for (size_t index = 0; index < count; ++index) {
      snapshot[index] = g_contributions[index];
    }
  }
  for (size_t index = 0; index < count; ++index) {
    if (snapshot[index].info_provider != nullptr) {
      snapshot[index].info_provider(payload);
    }
  }
}

} // namespace lunaredu::lunaros
