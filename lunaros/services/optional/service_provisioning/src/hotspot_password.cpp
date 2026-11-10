#include "hotspot_password.h"
#include "configuration/settings.h"
#include "configuration/lifecycle.h"
#include "provisioning_softap_password.h"
#include <esp_random.h>
namespace lunaredu::lunaros {
bool provisioning_hotspot_password(const std::string &override_password,
                                   std::string &out, std::string &error) {
  out.clear(); error.clear();
  // Recovery must not depend on writing business NVS to open its portal.
  // Use the declared product password in RAM; never replace a persisted secret
  // with a generated unknown one after a read failure.
  if (configuration_recovery_active()) {
    if (!is_provisioning_softap_password(override_password)) {
      error = "recovery_provisioning_password_required"; return false;
    }
    out = override_password; return true;
  }
  std::string saved;
  const auto result =
      settings_read_string("lunar_net", "ap_secret", 64, saved, error);
  if (result == SettingsReadResult::Failed)
    return false;
  if (!override_password.empty()) {
    if (!is_provisioning_softap_password(override_password)) {
      error = "invalid_provisioning_password";
      return false;
    }
    if (saved != override_password &&
        !settings_set_string("lunar_net", "ap_secret", override_password)) {
      error = "provisioning_password_save_failed";
      return false;
    }
    out = override_password;
    return true;
  }
  if (is_provisioning_softap_password(saved)) {
    out = std::move(saved);
    return true;
  }
  out = make_provisioning_softap_password(esp_random());
  if (!settings_set_string("lunar_net", "ap_secret", out)) {
    out.clear();
    error = "provisioning_password_save_failed";
    return false;
  }
  return true;
}
} // namespace lunaredu::lunaros
