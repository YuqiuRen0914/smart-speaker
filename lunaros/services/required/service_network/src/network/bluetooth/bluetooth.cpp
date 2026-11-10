#include "network/bluetooth/bluetooth_lifecycle.h"
#include "kernel/memory_admission.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if (defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED) || (defined(CONFIG_BT_BLUEDROID_ENABLED) && CONFIG_BT_BLUEDROID_ENABLED)
#include <array>
#include <atomic>
#include <cstring>
#include "bluetooth_registry.h"
#if CONFIG_BT_NIMBLE_ENABLED
#if CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_FREE || CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_BLOCK
#include <esp_heap_caps.h>
#endif
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include "lunaros_network_jobs.h"
#include "kernel/retry_schedule.h"
#include "platform/system.h"
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
extern "C" void ble_store_config_init(void);
#endif
namespace lunaredu::lunaros {
namespace {
using namespace bluetooth_internal;
#if CONFIG_BT_NIMBLE_ENABLED
bool cleanup_pending = false;
std::atomic<bool> retiring{false};
enum class SdkStage : uint8_t { None, Stop, Deinit };
SdkStage sdk_stage = SdkStage::None; // Network owner, immutable during a job.
NetworkJobHandle sdk_job;
NetworkRetirementWindow sdk_window; // Held through owner callbacks and deinit acknowledgement.
std::atomic<bool> sdk_done{false};
int sdk_result = 0; // Published by sdk_done, read only after job acknowledgement.
RetrySchedule sdk_retry{250, 4000}; // Network owner; failure completion starts cooldown.
bool profiles_prepared = false;

bool defer_sdk_retry(std::string &error) {
  sdk_retry.failed(system_monotonic_ms());
  // Physical ownership remains in retiring/running/cleanup_pending. A failed
  // SDK attempt must not monopolize the I/O scheduler during its cooldown.
  std::string release_error;
  if (sdk_window && !release_network_retirement_window(sdk_window, release_error))
    error = release_error;
  return false;
}

NetworkJobRunResult retire_sdk(NetworkJobHandle, void *) {
  network_job_runner_set_phase(NetworkJobPhase::ResourceCleanup);
  sdk_result = sdk_stage == SdkStage::Stop ? nimble_port_stop() : nimble_port_deinit();
  sdk_done.store(true, std::memory_order_release);
  return {sdk_result == 0, false,
          sdk_result == 0 ? "bluetooth_sdk_retired" : "bluetooth_sdk_retirement_failed"};
}
bool step_sdk(SdkStage stage, std::string &error) {
  if (sdk_job) {
    if (network_job_active(sdk_job)) { error = "bluetooth_stop_pending"; return false; }
    sdk_job = {};
    sdk_stage = SdkStage::None;
    if (sdk_done.exchange(false, std::memory_order_acquire)) {
      if (sdk_result == 0) { sdk_retry.reset(); error.clear(); return true; }
      error = "bluetooth_sdk_stop_failed:" + std::to_string(sdk_result);
      return defer_sdk_retry(error);
    }
    // A queued retirement can be cancelled before its runner executes. Retry
    // admission after a cooldown; cancellation is never proof that a physical resource exited.
    error = "bluetooth_sdk_job_cancelled";
    return defer_sdk_retry(error);
  }
  sdk_stage = stage;
  NetworkJobDefinition job;
  job.owner_service_id = "network.runtime";
  job.kind = stage == SdkStage::Stop ? "bluetooth.host.stop" : "bluetooth.host.deinit";
  job.priority = kNetworkPriorityCritical;
  job.job_class = NetworkJobClass::Retirement;
  job.runner = retire_sdk;
  if (!submit_network_job(job, sdk_job, error)) {
    sdk_stage = SdkStage::None;
    return defer_sdk_retry(error);
  }
  error = "bluetooth_stop_pending";
  return false;
}
void on_sync() {
  if (retiring.load()) return;
  synchronized.store(true);
  for (size_t i = 0; i < count; ++i)
    if (profiles[i].synchronized) profiles[i].synchronized(profiles[i].context);
}
void on_reset(int) {
  synchronized.store(false);
  for (size_t i = 0; i < count; ++i)
    if (profiles[i].reset) profiles[i].reset(profiles[i].context);
}
void host_task(void *) { nimble_port_run(); nimble_port_freertos_deinit(); }
#endif
}
bool register_bluetooth_profile(const BluetoothProfile &profile, std::string &error) {
  if (!profile.id || !*profile.id || !profile.install) {
    error = "invalid_bluetooth_profile"; return false;
  }
  for (size_t i = 0; i < count; ++i) if (std::strcmp(profiles[i].id, profile.id) == 0) {
    const auto &p = profiles[i];
    if (p.install == profile.install && p.synchronized == profile.synchronized &&
        p.reset == profile.reset && p.context == profile.context &&
        p.stopped == profile.stopped && p.poll == profile.poll && p.connected == profile.connected && p.prepare_stop == profile.prepare_stop) { error.clear(); return true; }
    error = "bluetooth_profile_conflict"; return false;
  }
  if (sealed || count == profiles.size()) { error = "bluetooth_profiles_closed"; return false; }
  profiles[count++] = profile; error.clear(); return true;
}
#if CONFIG_BT_NIMBLE_ENABLED
bool start_bluetooth(std::string &error) {
  if (retiring.load()) { error = "bluetooth_stop_pending"; return false; }
  if (running) { error.clear(); return true; }
  if (cleanup_pending) {
    auto cleanup = nimble_port_deinit();
    if (cleanup != ESP_OK) { error = esp_err_to_name(cleanup); return false; }
    cleanup_pending = false;
  }
  // Some controllers assert instead of returning an allocation error when
  // their exchange-memory block cannot be allocated. Products declare a
  // measured controller/host budget, as with the Bluedroid backend.
  // This is admission, not a reservation: later SDK allocations can still
  // fail, and the ordinary initialization error path remains necessary.
  MemoryStartWindow memory_window;
  MemoryStartRequirements memory;
#if CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_FREE
  memory.dma_free=CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_FREE;
#endif
#if CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_BLOCK
  memory.dma_largest=CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_BLOCK;
#endif
  if(!memory_start_gate().acquire(memory,memory_window,error)){
    if(error!="memory_start_busy")error="bluetooth_internal_memory_low";
    return false;
  }
  auto result = nimble_port_init();
  if (result != ESP_OK) { error = esp_err_to_name(result); return false; }
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
  for (size_t i = 0; i < count; ++i) {
    if (!profiles[i].install(profiles[i].context, error)) {
      cleanup_pending = nimble_port_deinit() != ESP_OK; return false;
    }
  }
  ble_store_config_init();
  result = esp_nimble_enable(reinterpret_cast<void *>(host_task));
  if (result != ESP_OK) {
    cleanup_pending = nimble_port_deinit() != ESP_OK; error = esp_err_to_name(result); return false;
  }
  profiles_prepared = false;
  sdk_retry.reset();
  running = true; sealed = true; error.clear(); return true;
}
bool stop_bluetooth(std::string &error) {
  if (!running && !cleanup_pending) {
    if (sdk_window && !release_network_retirement_window(sdk_window, error)) return false;
    sdk_retry.reset();
    profiles_prepared = false;
    retiring.store(false);
    error.clear(); return true;
  }
  retiring.store(true);
  if (!sdk_job && !sdk_retry.ready(system_monotonic_ms())) {
    error = "bluetooth_sdk_retry_pending";
    return false;
  }
  // Profiles quiesce on their declared owner. SDK waits run on the existing
  // blocking-I/O executor; callbacks and ownership transitions remain here.
  if (running) {
    if (!profiles_prepared) {
      for(size_t i=0;i<count;++i)if(profiles[i].prepare_stop&&!profiles[i].prepare_stop(profiles[i].context,error))return false;
      profiles_prepared = true;
    }
    if (!acquire_network_retirement_window(sdk_window, error)) return defer_sdk_retry(error);
    if (!step_sdk(SdkStage::Stop, error)) return false;
    running = false;
    cleanup_pending = true;
    synchronized.store(false);
    for (size_t i = 0; i < count; ++i)
      if (profiles[i].reset) profiles[i].reset(profiles[i].context);
    for (size_t i = 0; i < count; ++i)
      if (profiles[i].stopped) profiles[i].stopped(profiles[i].context);
  }
  if (!acquire_network_retirement_window(sdk_window, error)) return defer_sdk_retry(error);
  if (!step_sdk(SdkStage::Deinit, error)) return false;
  cleanup_pending = false;
  if (!release_network_retirement_window(sdk_window, error)) return false;
  sdk_retry.reset();
  profiles_prepared = false;
  retiring.store(false);
  error.clear(); return true;
}

#endif
void poll_bluetooth_profiles() {
#if CONFIG_BT_NIMBLE_ENABLED
  if (retiring.load()) {
    std::string error;
    (void)stop_bluetooth(error);
    if (retiring.load()) return;
  }
#endif
  for (size_t i=0;i<count;++i) if(profiles[i].poll) profiles[i].poll(profiles[i].context);
}
bool bluetooth_connected() {
  if(!running.load()) return false;
  for (size_t i=0;i<count;++i) if(profiles[i].connected && profiles[i].connected(profiles[i].context)) return true;
  return false;
}
bool bluetooth_synchronized() { return synchronized.load(); }
bool bluetooth_running() { return running.load(); }
#if CONFIG_BT_NIMBLE_ENABLED
bool bluetooth_occupied() { return running.load() || retiring.load() || cleanup_pending; }
#endif
}
#else
namespace lunaredu::lunaros {
bool register_bluetooth_profile(const BluetoothProfile &, std::string &e) { e = "bluetooth_disabled"; return false; }
bool start_bluetooth(std::string &e) { e = "bluetooth_disabled"; return false; }
bool stop_bluetooth(std::string &e) { e.clear(); return true; }
bool bluetooth_synchronized() { return false; }
bool bluetooth_running() { return false; }
bool bluetooth_occupied() { return false; }
void poll_bluetooth_profiles() {}
bool bluetooth_connected() { return false; }
}
#endif
