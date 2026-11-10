#include "runtime/configuration_bindings.h"
#include "configuration/settings.h"
#include "diagnostics/backend.h"
namespace lunaredu::lunaros {
namespace {
bool read_cpu_preference(bool &value, std::string &error) {
  const auto result = settings_read_bool("os.performance", "cpu_boot", value, error);
  if (result == SettingsReadResult::Missing) value = false;
  return result != SettingsReadResult::Failed;
}
bool write_cpu_preference(bool value, std::string &error) {
  if (settings_set_bool("os.performance", "cpu_boot", value)) { error.clear(); return true; }
  error = "cpu_monitor_settings_write_failed";
  return false;
}
}
bool bind_system_configuration(std::string &error) {
  if (!system::bind_cpu_monitor_preferences({read_cpu_preference, write_cpu_preference})) {
    error = "cpu_monitor_preferences_owner_conflict"; return false;
  }
  error.clear(); return true;
}
}
