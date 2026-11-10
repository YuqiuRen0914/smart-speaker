#include "bsp/lunaredu_bsp.h"
#include "boards/board_descriptor.h"

#include <stdio.h>
#include <string.h>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "sdkconfig.h"

namespace {

using lunaredu::bsp::ResourceDescriptor;
constexpr uint64_t pin_mask(uint8_t pin) { return 1ULL << pin; }
const auto &kResources = lunaredu::bsp::board_resources();
const ResourceDescriptor *descriptor_for(bsp_resource_t resource) {
  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) return nullptr;
  return &kResources[resource];
}
bool resource_available_for_profile(const ResourceDescriptor &descriptor) {
  return descriptor.available;
}

const char *pin_name(uint64_t pin) {
  switch (pin) {
  case pin_mask(0):
    return "GPIO0";
  case pin_mask(1):
    return "GPIO1";
  case pin_mask(2):
    return "GPIO2";
  case pin_mask(3):
    return "GPIO3";
  case pin_mask(4):
    return "GPIO4";
  case pin_mask(5):
    return "GPIO5";
  case pin_mask(6):
    return "GPIO6";
  case pin_mask(7):
    return "GPIO7";
  case pin_mask(8):
    return "GPIO8";
  case pin_mask(9):
    return "GPIO9";
  case pin_mask(10):
    return "GPIO10";
  case pin_mask(11):
    return "GPIO11";
  case pin_mask(12):
    return "GPIO12";
  case pin_mask(13):
    return "GPIO13";
  case pin_mask(14):
    return "GPIO14";
  case pin_mask(15):
    return "GPIO15";
  case pin_mask(16):
    return "GPIO16";
  case pin_mask(17):
    return "GPIO17";
  case pin_mask(18):
    return "GPIO18";
  case pin_mask(19):
    return "GPIO19";
  case pin_mask(20):
    return "GPIO20";
  case pin_mask(21):
    return "GPIO21";
  case pin_mask(38):
    return "GPIO38";
  case pin_mask(39):
    return "GPIO39";
  case pin_mask(40):
    return "GPIO40";
  case pin_mask(41):
    return "GPIO41";
  case pin_mask(42):
    return "GPIO42";
  case pin_mask(43):
    return "GPIO43";
  case pin_mask(44):
    return "GPIO44";
  case pin_mask(45):
    return "GPIO45";
  case pin_mask(46):
    return "GPIO46";
  case pin_mask(47):
    return "GPIO47";
  case pin_mask(48):
    return "GPIO48";
  default:
    return "GPIO?";
  }
}

constexpr size_t kOwnerNameMax = BSP_RESOURCE_CLAIM_OWNER_MAX;
constexpr size_t kClaimPinMax = BSP_RESOURCE_CLAIM_PIN_MAX;

const char *g_owners[BSP_RESOURCE_COUNT] = {};
char g_owner_storage[BSP_RESOURCE_COUNT][kOwnerNameMax] = {};
char g_last_claim_owner[kOwnerNameMax] = {};
bsp_resource_claim_t g_last_claim = {};

SemaphoreHandle_t resource_mutex() {
  static StaticSemaphore_t mutex_buffer;
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&mutex_buffer);
  return mutex;
}

class ResourceLock {
public:
  ResourceLock() : mutex_(resource_mutex()) {
    if (mutex_ != nullptr) {
      xSemaphoreTake(mutex_, portMAX_DELAY);
    }
  }

  ~ResourceLock() {
    if (mutex_ != nullptr) {
      xSemaphoreGive(mutex_);
    }
  }

private:
  SemaphoreHandle_t mutex_;
};

void copy_owner_name(char *target, size_t target_size, const char *owner) {
  if (target == nullptr || target_size == 0) {
    return;
  }
  snprintf(target, target_size, "%s", owner == nullptr ? "(unknown)" : owner);
}

void copy_optional_claim_text(char *target, size_t target_size,
                              const char *text) {
  if (target == nullptr || target_size == 0) {
    return;
  }
  snprintf(target, target_size, "%s", text == nullptr ? "" : text);
}

bsp_resource_claim_snapshot_t last_claim_snapshot_unlocked() {
  bsp_resource_claim_snapshot_t snapshot = {};
  snapshot.ok = g_last_claim.ok;
  copy_optional_claim_text(snapshot.owner, sizeof(snapshot.owner),
                           g_last_claim.owner);
  copy_optional_claim_text(snapshot.conflict_owner,
                           sizeof(snapshot.conflict_owner),
                           g_last_claim.conflict_owner);
  copy_optional_claim_text(snapshot.conflict_pin, kClaimPinMax,
                           g_last_claim.conflict_pin);
  return snapshot;
}

} // 匿名命名空间

esp_err_t bsp_resource_acquire(bsp_resource_t resource, const char *owner) {
  ResourceLock lock;

  if (owner == nullptr || owner[0] == '\0' ||
      std::strlen(owner) >= kOwnerNameMax) return ESP_ERR_INVALID_ARG;
  const ResourceDescriptor *descriptor = descriptor_for(resource);
  copy_owner_name(g_last_claim_owner, sizeof(g_last_claim_owner), owner);
  const char *claim_owner = g_last_claim_owner;

  g_last_claim = {
      .ok = false,
      .owner = claim_owner,
      .conflict_owner = nullptr,
      .conflict_pin = nullptr,
  };

  if (descriptor == nullptr) {
    g_last_claim.conflict_owner = "invalid_resource";
    return ESP_ERR_INVALID_ARG;
  }

  if (!resource_available_for_profile(*descriptor)) {
    g_last_claim.conflict_owner = "board_profile_unavailable";
    return ESP_ERR_NOT_SUPPORTED;
  }

  const char *current_owner = g_owners[resource];
  if (current_owner != nullptr) {
    if (strcmp(current_owner, claim_owner) == 0) {
      g_last_claim.ok = true;
      return ESP_OK;
    }
    g_last_claim.conflict_owner = current_owner;
    return ESP_ERR_INVALID_STATE;
  }

  for (const ResourceDescriptor &other : kResources) {
    const char *other_owner = g_owners[other.id];
    if (other_owner == nullptr) {
      continue;
    }

    const uint64_t conflict = descriptor->pin_mask & other.pin_mask;
    if (conflict != 0) {
      g_last_claim.conflict_owner = other_owner;
      g_last_claim.conflict_pin = pin_name(conflict & (~conflict + 1));
      return ESP_ERR_INVALID_STATE;
    }
  }

  copy_owner_name(g_owner_storage[resource], sizeof(g_owner_storage[resource]),
                  claim_owner);
  g_owners[resource] = g_owner_storage[resource];
  g_last_claim.ok = true;
  return ESP_OK;
}

esp_err_t bsp_resource_release(bsp_resource_t resource, const char *owner) {
  ResourceLock lock;

  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }

  const char *current_owner = g_owners[resource];
  if (current_owner == nullptr) {
    return ESP_OK;
  }

  if (owner != nullptr && strcmp(current_owner, owner) != 0) {
    return ESP_ERR_INVALID_STATE;
  }

  g_owners[resource] = nullptr;
  g_owner_storage[resource][0] = '\0';
  return ESP_OK;
}

bool bsp_resource_is_acquired(bsp_resource_t resource) {
  ResourceLock lock;

  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) {
    return false;
  }
  return g_owners[resource] != nullptr;
}

const char *bsp_resource_name(bsp_resource_t resource) {
  const ResourceDescriptor *descriptor = descriptor_for(resource);
  return descriptor == nullptr ? "unknown" : descriptor->name;
}

const char *bsp_resource_owner(bsp_resource_t resource) {
  ResourceLock lock;

  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) {
    return nullptr;
  }
  return g_owners[resource];
}

bool bsp_resource_observe(bsp_resource_t resource, bsp_resource_observation_t *out) {
  if (!out || resource < 0 || resource >= BSP_RESOURCE_COUNT) return false;
  ResourceLock lock;
  const auto *descriptor = descriptor_for(resource);
  if (!descriptor) return false;
  *out = {};
  out->name = descriptor->name;
  out->configured_available = descriptor->available;
  out->pin_mask = descriptor->pin_mask;
  if (g_owners[resource]) {
    std::strncpy(out->owner, g_owners[resource], sizeof(out->owner) - 1);
  }
  return true;
}

bool bsp_resource_contains_pin(bsp_resource_t resource, int pin) {
  const ResourceDescriptor *descriptor = descriptor_for(resource);
  if (descriptor == nullptr || !descriptor->available || pin < 0 || pin >= 64) {
    return false;
  }
  const uint64_t mask = 1ULL << static_cast<uint8_t>(pin);
  return (descriptor->pin_mask & mask) != 0;
}

const bsp_resource_claim_t *bsp_resource_last_claim() {
  ResourceLock lock;
  return &g_last_claim;
}

bsp_resource_claim_snapshot_t bsp_resource_last_claim_snapshot() {
  ResourceLock lock;
  return last_claim_snapshot_unlocked();
}

void bsp_resource_reset_all() {
  ResourceLock lock;

  for (const ResourceDescriptor &resource : kResources) {
    g_owners[resource.id] = nullptr;
    g_owner_storage[resource.id][0] = '\0';
  }
  g_last_claim_owner[0] = '\0';
  g_last_claim = {};
}

