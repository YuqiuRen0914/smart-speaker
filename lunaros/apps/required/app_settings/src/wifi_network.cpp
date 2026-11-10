#include "lunaros_network_scan.h"
#include "lunaros_network_jobs.h"
#include "lunaros_network_wifi_configuration.h"
#if defined(LUNAROS_SETTINGS_PROVISIONING_COMPONENT)
#include "lunaros_provisioning.h"
#endif
#include "lunaros_settings_wifi_runtime.h"
#include "desktop/shell/shell.h"
#include "kernel/runtime.h"
#include "lunaros_network.h"
#include "configuration/device_config.h"
#include <esp_wifi.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>

namespace lunaredu::desktop {
namespace {
namespace os=lunaredu::lunaros;
struct Operation {
  os::KernelRuntime *runtime=nullptr;
  WifiAccessPoint selected;
  char password[65]{};
  bool forget=false;
  std::atomic<uint32_t> attempt{0};
  uint32_t generation=0;
  std::atomic<WifiSubmission> result{WifiSubmission::Pending};
  ~Operation(){wipe_text(password,sizeof(password));}
};
struct NetworkBackend {
  os::KernelRuntime *runtime=nullptr;
  std::unique_ptr<Operation> operation;
  os::NetworkJobHandle job;
  os::NetworkWifiScanSnapshot scan;
} backend;
std::unique_ptr<WifiRuntime> foreground;
bool supported(uint8_t mode){
  return mode==WIFI_AUTH_OPEN || mode==WIFI_AUTH_WPA_PSK || mode==WIFI_AUTH_WPA2_PSK ||
      mode==WIFI_AUTH_WPA_WPA2_PSK || mode==WIFI_AUTH_WPA3_PSK || mode==WIFI_AUTH_WPA2_WPA3_PSK;
}
WifiObservation observe(void *opaque){
  auto &b=*static_cast<NetworkBackend *>(opaque);WifiObservation out;
  if(!b.runtime)return out;
  const auto network=os::network_snapshot();const auto fast=os::network_fast_snapshot();
  out.content.available=network.service_started && fast.station_enabled;
  out.content.editable=!network.fixed_wifi_credentials;
  out.network_sequence=network.sequence;out.failure_started_ms=network.disconnect_started_at_ms;
  out.link=network.link==os::NetworkLinkState::Up?WifiLink::Up:network.link==os::NetworkLinkState::Connecting?WifiLink::Connecting:WifiLink::Down;
  std::snprintf(out.target_ssid,sizeof(out.target_ssid),"%s",network.target_ssid.c_str());
  if(network.link==os::NetworkLinkState::Up)std::snprintf(out.content.connected_ssid,sizeof(out.content.connected_ssid),"%s",network.target_ssid.c_str());
  out.authentication_failed=network.last_disconnect_reason==WIFI_REASON_AUTH_FAIL || network.last_disconnect_reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || network.last_disconnect_reason==WIFI_REASON_HANDSHAKE_TIMEOUT;
  os::network_wifi_scan_snapshot_into(b.scan);out.scan_sequence=b.scan.sequence;
  out.content.scan=b.scan.state==os::NetworkWifiScanState::Ready?WifiScan::Ready:b.scan.state==os::NetworkWifiScanState::Failed?WifiScan::Failed:b.scan.state==os::NetworkWifiScanState::Scanning?WifiScan::Scanning:WifiScan::Idle;
  out.content.count=static_cast<uint8_t>(std::min(b.scan.access_point_count,out.content.networks.size()));
  const auto names=os::device_wifi_names(); // No password read on the input/render path.
  for(uint8_t index=0;index<names.count;++index)if(!std::strcmp(names.ssids[index].data(),out.content.connected_ssid))out.content.connected_saved_index=static_cast<int8_t>(index);
  for(size_t i=0;i<out.content.count;++i){
    const auto &ap=b.scan.access_points[i];auto &n=out.content.networks[i];
    std::snprintf(n.ssid,sizeof(n.ssid),"%s",ap.ssid.data());n.rssi=static_cast<int16_t>(ap.rssi);n.secure=ap.secure;n.supported=supported(ap.authmode);
    for(uint8_t index=0;index<names.count;++index)
      if(!std::strcmp(names.ssids[index].data(),n.ssid))n.saved_index=static_cast<int8_t>(index);
  }
  std::sort(out.content.networks.begin(),out.content.networks.begin()+out.content.count,[&](const auto &a,const auto &c){
    const bool current_a=!std::strcmp(a.ssid,out.content.connected_ssid),current_c=!std::strcmp(c.ssid,out.content.connected_ssid);
    if(current_a!=current_c)return current_a;
    if((a.saved_index>=0)!=(c.saved_index>=0))return a.saved_index>=0;
    return a.rssi>c.rssi;
  });return out;
}
bool scan(void *opaque){auto &b=*static_cast<NetworkBackend *>(opaque);std::string error;return b.runtime && os::request_network_wifi_scan(*b.runtime,error);}
os::NetworkJobRunResult run(os::NetworkJobHandle handle,void *opaque){
  auto &op=*static_cast<Operation *>(opaque);auto result=WifiSubmission::ApplyFailed;
  if(os::network_job_cancellation_requested(handle))result=WifiSubmission::Cancelled;
  else if(os::network_fast_snapshot().fixed_wifi_credentials)result=WifiSubmission::ApplyFailed;
  else if(op.forget){std::string error;result=os::request_network_forget_wifi(*op.runtime,op.selected.ssid,error)?WifiSubmission::Applied:error=="wifi_configuration_busy"?WifiSubmission::Busy:error=="wifi_save_failed"?WifiSubmission::SaveFailed:WifiSubmission::ApplyFailed;
  }else {
    uint32_t attempt=0;
    const auto status=op.selected.saved_index>=0?os::request_network_connect_saved_wifi(*op.runtime,op.selected.ssid,attempt):os::request_network_connect_wifi(*op.runtime,op.selected.ssid,op.password,attempt);
    op.attempt.store(attempt,std::memory_order_release);
    result=status==os::NetworkWifiCredentialResult::Ok?WifiSubmission::Applied:status==os::NetworkWifiCredentialResult::Busy?WifiSubmission::Busy:WifiSubmission::ApplyFailed;
  }
  wipe_text(op.password,sizeof(op.password));op.result.store(result,std::memory_order_release);
  return {result==WifiSubmission::Applied,false,result==WifiSubmission::Applied?"desktop_wifi_applied":"desktop_wifi_request_failed"};
}
bool submit(void *opaque,const WifiAccessPoint &selected,const char *password,bool forget,uint32_t generation){
  auto &b=*static_cast<NetworkBackend *>(opaque);
  if(!b.runtime || (b.job && os::network_job_active(b.job)))return false;
  auto op=std::make_unique<Operation>();op->runtime=b.runtime;op->selected=selected;op->forget=forget;op->generation=generation;
  std::snprintf(op->password,sizeof(op->password),"%s",password?password:"");b.operation=std::move(op);
  os::NetworkJobDefinition job;job.owner_service_id="desktop.wifi";job.kind=forget?"wifi.forget":"wifi.connect";
  job.priority=os::kNetworkPriorityInteractive;job.runner=run;job.context=b.operation.get();
  std::string error;
  if(!os::submit_network_job(job,b.job,error)){b.operation.reset();b.job={};return false;}
  return true;
}
WifiSubmission submission(void *opaque,uint32_t generation){
  auto &b=*static_cast<NetworkBackend *>(opaque);
  if(!b.operation || b.operation->generation!=generation)return WifiSubmission::Cancelled;
  const auto result=b.operation->result.load(std::memory_order_acquire);
  if(result==WifiSubmission::Pending)return os::network_job_active(b.job)?result:WifiSubmission::Cancelled;
  if(result!=WifiSubmission::Applied||b.operation->forget)return result;
  const uint32_t id=b.operation->attempt.load(std::memory_order_acquire);const auto attempt=os::network_wifi_connection_snapshot();
  if(!id||attempt.attempt!=id)return WifiSubmission::Cancelled;
  switch(attempt.phase){
    case os::NetworkWifiConnectionPhase::Connecting:case os::NetworkWifiConnectionPhase::Saving:return WifiSubmission::Pending;
    case os::NetworkWifiConnectionPhase::Connected:return WifiSubmission::Applied;
    case os::NetworkWifiConnectionPhase::ConnectedUnsaved:return WifiSubmission::ConnectedUnsaved;
    case os::NetworkWifiConnectionPhase::Cancelled:return WifiSubmission::Cancelled;
    default:return WifiSubmission::ApplyFailed;
  }
}
void abandon(void *opaque){
  auto &b=*static_cast<NetworkBackend *>(opaque);std::string ignored;
  (void)ignored; // Page exit only discards its draft; accepted work continues.
  // Keep context alive until a running callback has returned. A committed
  // save/connect is not rolled back merely because the user left the page.
  if(!b.job || !os::network_job_active(b.job)){b.operation.reset();b.job={};}
}
}
void configure_desktop_wifi(os::KernelRuntime *runtime){
  if(backend.job && os::network_job_active(backend.job))return;
  backend.runtime=runtime;foreground.reset(new WifiRuntime({&backend,observe,scan,submit,submission,abandon}));
}
bool update_desktop_wifi(DesktopShell &shell,uint32_t now,bool visible){
  if(!foreground)foreground.reset(new WifiRuntime({&backend,observe,scan,submit,submission,abandon}));
  const bool changed=foreground->update(shell,now,visible);
  static uint32_t last_controls_poll=0;
  if(static_cast<uint32_t>(now-last_controls_poll)<125)return changed;
  last_controls_poll=now;
  const auto network=os::network_fast_snapshot();bool controls_changed=false;
#if defined(LUNAROS_SETTINGS_PROVISIONING_COMPONENT)
  const auto setup=os::provisioning_fast_snapshot();
  controls_changed|=shell.set_menu_value("os.provisioning",setup.state==os::ProvisioningState::Preparing?"正在开启":setup.state==os::ProvisioningState::Stopping?"正在关闭":setup.state!=os::ProvisioningState::Idle?"开启":"关闭");
#endif
  controls_changed|=shell.set_menu_value("os.bluetooth",network.bluetooth_enabled?"开启":"关闭");
  if((!visible || shell.surface!=DesktopSurface::Wifi) && backend.operation)abandon(&backend);
  return changed||controls_changed;
}
} // namespace lunaredu::desktop
