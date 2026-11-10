#include "lunaros_network_control.h"
#include "lunaros_network_jobs.h"
#include "lunaros_settings_wifi_runtime.h"
#include <esp_timer.h>
#include <atomic>
#include <mutex>

namespace lunaredu::settings {
namespace {
struct ControlOperation {
  lunaros::KernelRuntime *runtime=nullptr;
  NetworkControl control=NetworkControl::BluetoothOn;
  std::atomic<int> result{1};
} control_operation;
lunaros::NetworkJobHandle control_job;
std::mutex control_mutex;
lunaros::NetworkJobRunResult run_control(lunaros::NetworkJobHandle handle,void *opaque) {
  auto &op=*static_cast<ControlOperation *>(opaque);std::string error;bool ok=false;
  if(!lunaros::network_job_cancellation_requested(handle)) {
    switch(op.control) {
      case NetworkControl::BluetoothOn:case NetworkControl::BluetoothOff:ok=lunaros::request_network_set_bluetooth(*op.runtime,op.control==NetworkControl::BluetoothOn,error);break;
    }
  }
  op.result.store(ok?1:-1,std::memory_order_release);
  const bool retry=error=="ble_disconnect_pending";
  const bool retiring = ok && op.control == NetworkControl::BluetoothOff &&
      lunaros::network_bluetooth_stop_pending();
  return {ok,retry,ok?(retiring?"settings_network_accepted":"settings_network_applied"):error};
}
}
bool queue_network_control(lunaros::KernelRuntime &runtime,NetworkControl control,std::string &error) {
  std::lock_guard<std::mutex> lock(control_mutex);
  if((control_job&&lunaros::network_job_active(control_job)) ||
      lunaros::network_bluetooth_stop_pending()){error="settings_network_busy";return false;}
  control_operation.runtime=&runtime;control_operation.control=control;control_operation.result.store(0,std::memory_order_release);
  lunaros::NetworkJobDefinition job;job.owner_service_id="settings.network";job.kind="network.control";
  job.priority=lunaros::kNetworkPriorityInteractive;job.runner=run_control;job.context=&control_operation;
  job.maximum_attempts=30;job.retry_backoff_initial_ms=100;job.retry_backoff_maximum_ms=250;
  job.deadline_ms=static_cast<uint32_t>(esp_timer_get_time()/1000)+15000;
  if(!job.deadline_ms)job.deadline_ms=1;
  if(!lunaros::submit_network_job(job,control_job,error)){control_operation.result.store(-1);return false;}
  return true;
}
NetworkControlStatus network_control_status() {
  std::lock_guard<std::mutex> lock(control_mutex);
  const bool busy=(control_job&&lunaros::network_job_active(control_job)) ||
      lunaros::network_bluetooth_stop_pending();
  return {busy,!busy&&control_operation.result.load(std::memory_order_acquire)!=1,control_operation.control};
}
}
