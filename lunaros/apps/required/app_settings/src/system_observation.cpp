#include "lunaros_provisioning_policy.h"
#if defined(LUNAROS_SETTINGS_PROVISIONING_COMPONENT)
#include "lunaros_provisioning.h"
#endif
#include "lunaros_settings_system_observation.h"
#include "lunaros_network.h"
#include "lunaros_settings_wifi_runtime.h"
#if defined(LUNAROS_SETTINGS_OTA_COMPONENT)
#include "lunaredu_ota.h"
#endif
namespace lunaredu::settings {
SystemPresentation SystemObservation::capture(int64_t now_us,bool updating,bool platform_available) {
  SystemPresentation result;SystemObservationCursor cursor;
  capture_into(result,cursor,now_us,updating,platform_available);return result;
}
void SystemObservation::capture_into(SystemPresentation &result,SystemObservationCursor &cursor,
    int64_t now_us,bool updating,bool platform_available,const UpdateProgressObservation *progress) {
  if(!revision_||now_us<observed_at_us_||now_us-observed_at_us_>=interval_us_) {
    const auto network=lunaros::network_fast_snapshot();
    if (!have_network_ || network.sequence != network_sequence_) {
      const auto detail=lunaros::network_snapshot();
      pending_.wifi_connected=detail.link==lunaros::NetworkLinkState::Up&&!detail.local_ip.empty();
      pending_.bluetooth_available=detail.bluetooth_available;pending_.bluetooth_enabled=detail.bluetooth_enabled;
      pending_.bluetooth_connected=detail.bluetooth_connected;
      pending_.wifi_ssid=detail.target_ssid;pending_.local_ip=detail.local_ip;
      network_sequence_=detail.sequence;have_network_=true;
    }
    pending_.platform_connected=platform_available&&platform_connected_&&platform_connected_();
#if defined(LUNAROS_SETTINGS_PROVISIONING_COMPONENT)
    const auto setup=lunaros::provisioning_snapshot();pending_.provisioning=setup.state!=lunaros::ProvisioningState::Idle;
    pending_.provisioning_available=setup.enabled&&setup.mode!=lunaredu::lunaros::ProvisioningMode::Direct;pending_.provisioning_ble=setup.mode==lunaredu::lunaros::ProvisioningMode::SoftApBle&&pending_.bluetooth_enabled;
    pending_.setup_remaining_seconds=unsigned(setup.remaining_ms/1000);
    pending_.setup_ssid=setup.ssid;pending_.setup_ip=setup.ip;
    pending_.setup_password=pending_.provisioning?lunaros::provisioning_local_password():"";
    pending_.setup_url=pending_.provisioning?lunaros::provisioning_local_url():"";
    pending_.setup_phase=setup.progress.phase;pending_.setup_error=setup.progress.error.empty()?setup.error:setup.progress.error;
#endif
    if(platform_available&&observe_details_)observe_details_(pending_,now_us);
    observed_at_us_=now_us;
    if(!revision_||!(pending_==cached_)){cached_=pending_;++revision_;}
  }
  if(cursor.source!=this||cursor.revision!=revision_) {
    result=cached_;cursor={this,revision_};
  }
  result.updating=updating;result.platform_available=platform_available;
  result.update_percent=0;result.update_progress_known=false;result.update_restarting=false;
  if(updating&&progress){
    result.update_percent=progress->percent;result.update_progress_known=progress->known;
    result.update_restarting=progress->restarting;
  }
#if defined(LUNAROS_SETTINGS_OTA_COMPONENT)
  else if(updating){
    const auto ota=lunaros::ota_service_progress_snapshot();
    result.update_percent=ota.pull_progress_percent;result.update_progress_known=ota.pull_total_bytes>0;
    result.update_restarting=ota.restart_pending;
  }
#endif
  const auto control=network_control_status();result.network_busy=control.busy;result.network_failed=control.failed;
  result.activity_phase=unsigned(now_us/100000)%12;
}
}
