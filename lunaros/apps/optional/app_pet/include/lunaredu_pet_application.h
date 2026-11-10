#pragma once
#include "app/lifecycle/app_capability.h"
#include "app/lifecycle/application.h"
#include "kernel/service.h"
#include <cstdint>
#include <string>
namespace lunaredu::lunaros { class KernelRuntime; }
namespace lunaredu::pet {
inline constexpr const char *kApplicationId = "lunar-pet";
inline constexpr const char *kDisplayServiceId = "pet.display";
inline constexpr const char *kAnimationServiceId = "pet.animation";
bool register_application_capabilities(lunaredu::app::CapabilityRegistry &catalog, std::string &error);
lunaredu::lunaros::ServiceNode animation_service(lunaredu::lunaros::KernelRuntime &runtime);
lunaredu::app::ForegroundDefinition application();
}
