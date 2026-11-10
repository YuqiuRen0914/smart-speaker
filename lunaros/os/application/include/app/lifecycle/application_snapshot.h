#pragma once

#include <array>
#include "app/lifecycle/application_state.h"
#include <stddef.h>
#include <stdint.h>
#include <string>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include "app/lifecycle/application.h"

#ifndef CONFIG_LUNAREDU_LUNAROS_FOREGROUND_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_FOREGROUND_CAPACITY 16
#endif

#ifndef CONFIG_LUNAREDU_LUNAROS_APPLICATION_CAPABILITY_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_APPLICATION_CAPABILITY_CAPACITY 8
#endif

#ifndef CONFIG_LUNAREDU_LUNAROS_APPLICATION_SERVICE_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_APPLICATION_SERVICE_CAPACITY 4
#endif

#ifndef CONFIG_LUNAREDU_LUNAROS_APPLICATION_RESOURCE_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_APPLICATION_RESOURCE_CAPACITY 4
#endif


namespace lunaredu::app {

// A bounded registry is intentional on ESP32, but the bound is runtime
// headroom rather than a product application-count contract. Products may use
// the same controller for launcher applications, system surfaces, and modes.
constexpr size_t kForegroundCapacity =
    CONFIG_LUNAREDU_LUNAROS_FOREGROUND_CAPACITY;
constexpr size_t kApplicationCapacity = kForegroundCapacity;
constexpr size_t kApplicationCapabilityCapacity =
    CONFIG_LUNAREDU_LUNAROS_APPLICATION_CAPABILITY_CAPACITY;
constexpr size_t kApplicationServiceCapacity =
    CONFIG_LUNAREDU_LUNAROS_APPLICATION_SERVICE_CAPACITY;
constexpr size_t kApplicationResourceCapacity =
    CONFIG_LUNAREDU_LUNAROS_APPLICATION_RESOURCE_CAPACITY;





struct ApplicationSnapshot {
  std::string id;
  std::string title;
  std::string icon;
  uint32_t launcher_accent_rgb = 0x38BDF8;
  std::string version;
  std::string settings_route;
  ForegroundKind kind = ForegroundKind::Application;
  ApplicationBackend backend = ApplicationBackend::Native;
  ApplicationSystemUiPolicy system_ui = ApplicationSystemUiPolicy::Standard;
  bool is_home = false;
  bool available = true;
  std::string unavailable_reason;
  bool has_preparation = false;
  ApplicationPreparationStatus preparation;
  size_t capability_count = 0;
  struct Capability {
    std::string id;
    bool required = true;
    bool available = false;
  };
  std::array<Capability, kApplicationCapabilityCapacity> capabilities;
  size_t service_count = 0;
  struct Service {
    // Application registrations are immutable after setup(), so snapshots can
    // reference the stable id instead of copying dozens of strings onto the
    // LunarOS owner task stack.
    const char *id = nullptr;
    bool required = true;
    bool available = false;
  };
  std::array<Service, kApplicationServiceCapacity> services;
  size_t resource_count = 0;
  struct Resource {
    std::string id;
    lunaros::ResourceAccess access = lunaros::ResourceAccess::Exclusive;
  };
  std::array<Resource, kApplicationResourceCapacity> resources;
  bool active = false;
  bool pending = false;
};

// Small, cross-executor control-plane view. Keep this type independent from
// the fixed-capacity application catalog so HTTP/network tasks never need to
// place the complete catalog on their stack just to read transition state.
struct AppControllerStatusSnapshot {
  AppControllerState state = AppControllerState::Stopped;
  std::string active_application_id;
  std::string pending_application_id;
  std::string previous_application_id;
  std::string last_error;
  uint32_t transition_id = 0;
  AppTransitionPhase transition_phase = AppTransitionPhase::Idle;
  size_t application_count = 0;
};

// Full catalog snapshots are intended for long-lived owner storage (for
// example the desktop shell's PSRAM cache) and host-side tests. Device contracts use
// AppControllerStatusSnapshot plus one ApplicationSnapshot at a time.
struct AppControllerSnapshot : AppControllerStatusSnapshot {
  std::array<ApplicationSnapshot, kApplicationCapacity> applications;
};


} // namespace lunaredu::app
