#pragma once

#include "device_api/device_projection.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::app { class CapabilityRegistry; }

namespace lunaredu::pet {
struct DesktopSceneStatus;

// Pet owns the external desktop_pet protocol. Products supply optional state
// and operations from their installed services.
struct PetDeviceProjectionConfig {
  void (*contribute_product_state)(JsonObject) = nullptr;
  void (*contribute_performance_state)(JsonObject) = nullptr;
  void (*contribute_motion_state)(lunaredu::lunaros::DeviceApiContext &, JsonObject) = nullptr;
  void (*contribute_extension_state)(JsonObject) = nullptr;
  void (*contribute_performance_contract)(JsonObject) = nullptr;
  void (*contribute_motion_contract)(JsonObject) = nullptr;
  void (*contribute_extension_contract)(JsonObject) = nullptr;
  bool (*submit_display_intent)(const char *) = nullptr;
  bool (*stop_all_motion)(app::CapabilityRegistry &, std::string &, std::string &) = nullptr;
  const lunaredu::lunaros::DeviceOperationDefinition *motion_operations = nullptr;
  size_t motion_operation_count = 0;
  const lunaredu::lunaros::DeviceOperationDefinition *additional_operations = nullptr;
  size_t additional_operation_count = 0;
  const lunaredu::lunaros::DeviceOperationDefinition *performance_operations = nullptr;
  size_t performance_operation_count = 0;
  const char *description = nullptr;
  const char *product_variant = nullptr;
  const char *const *supported_targets = nullptr;
  size_t supported_target_count = 0;
  bool motion_supported = true;
  bool servo_supported = true;
  bool touch_supported = true;
  float max_speed = 1;
  uint32_t max_duration_ms = 2400;
  // Optional desktop presentation owner. Pet serializes its own rendering and
  // scene telemetry; products only select the observer, never mirror fields.
  DesktopSceneStatus (*observe_desktop_scene)() = nullptr;
};

bool register_pet_device_projection(
    lunaredu::lunaros::DeviceProjectionRegistry &,
    const PetDeviceProjectionConfig &, std::string &);
bool set_pet_device_projection_available(
    lunaredu::lunaros::DeviceRuntimeContext &, bool, std::string &);
void fill_composed_device_state(lunaredu::lunaros::DeviceApiContext &, JsonObject);

} // namespace lunaredu::pet
