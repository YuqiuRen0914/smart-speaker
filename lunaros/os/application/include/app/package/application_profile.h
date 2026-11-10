#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lunaredu::lunaros::apps {
// Versioned application contract. Logical guest coordinates are independent
// of the product's physical panel; changing this requires an ABI decision.
struct ApplicationPlatformProfile {
  static constexpr uint32_t abi = 1;
  static constexpr uint32_t package_version = 2;
  static constexpr int canvas_width = 240;
  static constexpr int canvas_height = 240;
  static constexpr int maximum_tick_hz = 60;
  static constexpr size_t maximum_name_bytes = 16;
  static constexpr size_t maximum_name_characters = 4;
  static constexpr size_t maximum_description_bytes = 900;
  static constexpr size_t maximum_icon_resource_bytes = 47;
};
struct ApplicationRuntimeBudget {
  static constexpr size_t applications = 16;
  static constexpr size_t catalog_bytes = 32 * 1024;
  static constexpr size_t package_bytes = 192 * 1024;
  static constexpr size_t save_bytes = 8192;
  static constexpr uint32_t wasm_memory_pages = 4;
  static constexpr size_t draw_bytes = 32768;
  static constexpr size_t draw_commands = 1024;
  static constexpr size_t input_events = 64;
  static constexpr size_t executor_stack_bytes = 12288;
};
struct ApplicationBackendContract {
  std::string_view id;
  bool declares_memory_pages;
  bool supports_device_basic;
};
inline constexpr ApplicationPlatformProfile kApplicationPlatform{};
inline constexpr ApplicationRuntimeBudget kApplicationBudget{};
inline constexpr ApplicationBackendContract kApplicationBackends[] = {
  {"wasm", true, true}, {"micropython", false, false}
};
inline constexpr std::string_view kApplicationPermissions[] = {
  "graphics", "input", "storage.private", "device.basic"
};
constexpr const ApplicationBackendContract *application_backend_contract(std::string_view id) {
  for (const auto &backend : kApplicationBackends) if (backend.id == id) return &backend;
  return nullptr;
}
} // namespace lunaredu::lunaros::apps
