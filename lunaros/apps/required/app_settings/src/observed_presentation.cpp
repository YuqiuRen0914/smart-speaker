#include "lunaros_settings_observed_presentation.h"
#include <tuple>

namespace lunaredu::settings {
bool SystemPresentation::operator==(const SystemPresentation &s) const {
    return std::tie(wifi_connected,platform_connected,platform_available,provisioning,updating,
      provisioning_available,provisioning_ble,setup_remaining_seconds,setup_phase,setup_error,
      bluetooth_available,bluetooth_enabled,bluetooth_connected,network_busy,network_failed,bluetooth,bluetooth_scroll,
      wifi_ssid,local_ip,setup_ssid,setup_password,setup_ip,setup_url,update_check,platform_bound,pairing_pending,
      pairing_code,pairing_url,pairing_error,account_name,account_avatar,pairing_seconds,available_version,update_error,
      operation_message,update_percent,activity_phase,update_progress_known,update_restarting)==
      std::tie(s.wifi_connected,s.platform_connected,s.platform_available,s.provisioning,s.updating,
      s.provisioning_available,s.provisioning_ble,s.setup_remaining_seconds,s.setup_phase,s.setup_error,
      s.bluetooth_available,s.bluetooth_enabled,s.bluetooth_connected,s.network_busy,s.network_failed,s.bluetooth,s.bluetooth_scroll,
      s.wifi_ssid,s.local_ip,s.setup_ssid,s.setup_password,s.setup_ip,s.setup_url,s.update_check,s.platform_bound,s.pairing_pending,
      s.pairing_code,s.pairing_url,s.pairing_error,s.account_name,s.account_avatar,s.pairing_seconds,s.available_version,s.update_error,
      s.operation_message,s.update_percent,s.activity_phase,s.update_progress_known,s.update_restarting);
  }

void observe_system_presentation(SystemPresentation &target,SystemObservationCursor &cursor,
    const SystemPresentation &sample,const SystemObservationCursor &version) {
  if(!version.source||cursor.source!=version.source||cursor.revision!=version.revision) {
    target=sample;cursor=version;return;
  }
  target.updating=sample.updating;target.platform_available=sample.platform_available;
  target.network_busy=sample.network_busy;target.network_failed=sample.network_failed;
  target.activity_phase=sample.activity_phase;
  target.update_percent=sample.update_percent;target.update_progress_known=sample.update_progress_known;
  target.update_restarting=sample.update_restarting;
  target.update_error=sample.update_error;target.operation_message=sample.operation_message;
}
}
