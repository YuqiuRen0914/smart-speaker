#pragma once

#include <array>
#include <string>

namespace lunaredu::lunaros {

// Explicit static contributions: an unselected installer has no link reference.
struct StandardProjectionContribution {
  const char *id;
  bool (*install)(std::string &error);
};

bool setup_service_projection(std::string &error);
bool setup_task_projection(std::string &error);
bool setup_identity_projection(std::string &error);
bool setup_system_projection(std::string &error);
bool setup_storage_projection(std::string &error);
bool setup_network_projections(std::string &error);
bool setup_device_link_projection(std::string &error);
bool setup_update_projection(std::string &error);

namespace standard_projection {
inline constexpr StandardProjectionContribution device_link{"device_link", setup_device_link_projection};
inline constexpr StandardProjectionContribution services{"service", setup_service_projection};
inline constexpr StandardProjectionContribution tasks{"runtime", setup_task_projection};
inline constexpr StandardProjectionContribution identity{"identity", setup_identity_projection};
inline constexpr StandardProjectionContribution system{"system", setup_system_projection};
inline constexpr StandardProjectionContribution storage{"storage", setup_storage_projection};
inline constexpr StandardProjectionContribution network{"network", setup_network_projections};
inline constexpr StandardProjectionContribution update{"update", setup_update_projection};
}

bool install_standard_device_projections(const StandardProjectionContribution *contributions,
                                         size_t count, std::string &error);

template <const StandardProjectionContribution &... Contributions>
bool setup_standard_device_projections(std::string &error) {
  static constexpr std::array<StandardProjectionContribution, sizeof...(Contributions)> entries{Contributions...};
  return install_standard_device_projections(entries.data(), entries.size(), error);
}

} // namespace lunaredu::lunaros
