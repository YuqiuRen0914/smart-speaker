#include "lunaros_firmware_installation.h"
#include "configuration/settings.h"
#include "lunaredu_ota.h"
#include "lunaros_ota_lifecycle.h"
#include "lunaros_resource_managed_assets.h"
#include "lunaros_resource_install_plan.h"
#include "lunaros_storage.h"
#include "device_api/device_operation_builders.h"
#include "device_api/device_projection.h"
#include "model/device_model.h"
#include "device_api/bootstrap_metrics_json.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <string_view>
#include <atomic>
#include <mutex>
#include <esp_timer.h>

namespace lunaredu::lunaros {
namespace installation {
bool validate_firmware_installation_assets(std::string &);
bool resume_firmware_installation(std::string &);
bool begin_firmware_installation(std::string &);
bool complete_firmware_installation(std::string &);
bool firmware_installation_pending();
namespace {
std::atomic<bool> active{false}, completed{false};
std::atomic<bool> recovery_mode{false};
FirmwareInstallationPolicy policy;
bool configured=false;
const char *kNamespace="fw_install";
constexpr const char *kKey="pending_elf", *kPlanKey="plan";
constexpr size_t kPlanBytes = 4096;
std::array<const char *,kManagedAssetCapacity> kResources{};
size_t resource_count=0;
using DesiredResource=ManagedAssetInstallTarget;
ManagedAssetInstallPlan<kManagedAssetCapacity> install_plan{kResources,0};
std::array<DesiredResource, kManagedAssetCapacity> desired;
std::mutex desired_mutex;
bool desired_valid = false;
bool local_plan = false;
std::string phase = "inactive", last_error;
uint64_t desired_generation = 0;
bool cloud_restart_pending = false;
int64_t next_completion_check = 0;
bool valid_identity(const std::string &value) {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool firmware_ready() {
  const auto ota = ota_service_status_snapshot();
  return ota.boot_confirmed && !ota.pending_verify && !ota.upload_active &&
      !ota.pull_queued && !ota.pull_active && !ota.restart_pending;
}
// This record is an installation intent, scoped to this running image. It is
// not a history of resource versions or a rollback selection mechanism.
bool persist_plan(const std::array<DesiredResource, kManagedAssetCapacity> &next, bool local) {
  JsonDocument document;
  document["elf"] = ota_writer_metadata().app_elf_sha256;
  document["local"] = local;
  install_plan.encode(document["assets"].to<JsonArray>(),next);
  std::string encoded; serializeJson(document, encoded);
  return encoded.size() <= kPlanBytes && settings_set_string(kNamespace, kPlanKey, encoded);
}
bool accept_plan(JsonVariantConst assets, bool local, std::string &error) {
  if (policy.existing_assets_only) { error = "installation_disabled_boot_validation"; return false; }
  std::array<DesiredResource, kManagedAssetCapacity> next;
  if (!install_plan.parse(assets, next)) { error = "installation_manifest_invalid"; return false; }
  std::lock_guard lock(desired_mutex);
  // An explicit authorized install plan cannot be replaced by an unrelated
  // global cloud catalog while this transaction is in progress.
  if (local_plan && !local) {
    // The cloud may renew a locator for the exact authorized content, but
    // cannot replace a local installation's selected revisions or digests.
    if (!install_plan.same_content(next,desired)) return true;
    local = true;
  }
  if (desired_valid && desired == next && local_plan == local) return true;
  if (!persist_plan(next, local)) { last_error = error = "installation_plan_commit_failed"; return false; }
  desired = std::move(next); desired_valid = true; local_plan = local;
  ++desired_generation; last_error.clear(); phase = "waiting_firmware";
  return true;
}
void status(DeviceApiContext &device, JsonObject payload) {
  payload["active"] = active.load();
  payload["completed"] = completed.load();
  payload["firmware_confirmed"] = ota_service_status_snapshot().boot_confirmed;
  payload["mode"] = recovery_mode ? "recovery" : "installation";
  fill_bootstrap_metrics_payload(device, payload["bootstrap"].to<JsonObject>());
  std::lock_guard lock(desired_mutex);
  payload["phase"] = phase;
  payload["error"] = last_error;
  payload["plan_source"] = desired_valid ? (local_plan ? "management" : "platform") : "none";
}
DeviceOperationResult resume(DeviceApiContext &, JsonVariantConst, JsonObject payload) {
  std::string error;
  if (!resume_firmware_installation(error))
    return device_operation_error(error.c_str(), 409, error.c_str());
  payload["accepted"] = true;
  return device_operation_ack();
}
DeviceOperationResult set_plan(DeviceApiContext &, JsonVariantConst request, JsonObject payload) {
  if (policy.existing_assets_only) return device_operation_error(
      "installation_disabled_boot_validation", 409, "installation_disabled_boot_validation");
  if (!active || completed) return device_operation_error("installation_not_active", 409, "installation_not_active");
  if (!firmware_ready()) return device_operation_error("installation_firmware_not_ready", 409, "installation_firmware_not_ready");
  std::string error;
  if (!accept_plan(request["assets"], true, error))
    return device_operation_error(error.c_str(), error == "installation_manifest_invalid" ? 400 : 503, error.c_str());
  payload["accepted"] = true;
  return device_operation_ack();
}
DeviceOperationResult complete(DeviceApiContext &, JsonVariantConst, JsonObject payload) {
  std::string error;
  if (!complete_firmware_installation(error))
    return device_operation_error(error.c_str(), 409, "installation_incomplete");
  payload["completed"] = true;
  payload["restart_required"] = true;
  return device_operation_ack();
}
constexpr DeviceOperationDefinition operations[] = {
    device_operation_on_caller(make_device_json_provider("status.get", status)),
    make_device_json_command("plan.set", set_plan),
    make_device_json_command("resume", resume),
    make_device_json_command("complete", complete)};
DeviceCapabilityDescriptor capability = [] {
  DeviceCapabilityDescriptor result;
  result.id = "firmware_installation";
  return result;
}();
DeviceModuleProjectionMetadata module{"firmware_installation", "installation"};
DeviceModuleProjection projection{capability.id, &module, operations, std::size(operations)};
}
bool firmware_installation_pending() {
  if(!policy.transaction_required)return false;
  if (ota_service_image_requires_confirmation()) return true;
  std::string marker, error;
  const auto result = settings_read_string(kNamespace, kKey, 64, marker, error);
  // A failed read cannot prove that the firmware/resource transaction finished.
  return result == SettingsReadResult::Failed ||
      (result == SettingsReadResult::Found &&
       (!valid_identity(marker) || marker == ota_writer_metadata().app_elf_sha256));
}
bool validate_firmware_installation_assets(std::string &error) {
  error.clear();
  if (!storage_initialize()) { error = "existing_assets_storage_unavailable"; return false; }
  std::string failed;
  if(!validate_managed_assets(kResources.data(),resource_count,failed,error)){
    error="existing_assets_"+failed+":"+error;return false;
  }
  return true;
}
void configure_firmware_installation_recovery(bool enabled) {
  configure_managed_asset_startup_recovery(false);
  recovery_mode = enabled;
  std::lock_guard lock(desired_mutex);
  phase = enabled ? "management_recovery" : "inactive";
}
bool resume_firmware_installation(std::string &error) {
  if (policy.existing_assets_only) { error = "installation_disabled_boot_validation"; return false; }
  if (!recovery_mode) { error = "installation_recovery_not_active"; return false; }
  if (!firmware_ready()) { error = "installation_firmware_not_ready"; return false; }
  if (active || completed) return true;
  if (!firmware_installation_pending()) { error = "installation_not_pending"; return false; }
  return begin_firmware_installation(error);
}
bool begin_firmware_installation(std::string &error) {
  if(!policy.transaction_required){error="installation_not_required";return false;}
  if (policy.existing_assets_only) { error = "installation_disabled_boot_validation"; return false; }
  configure_managed_asset_startup_recovery(false);
  completed = false;
  cloud_restart_pending = false;
  next_completion_check = 0;
  const auto identity = ota_writer_metadata().app_elf_sha256;
  active = valid_identity(identity) && settings_set_string(kNamespace, kKey, identity);
  error = active ? "" : "installation_stage_commit_failed";
  std::lock_guard lock(desired_mutex);
  desired = {}; desired_valid = false; local_plan = false; ++desired_generation;
  phase = active ? "waiting_manifest" : "failed"; last_error = error;
  if (active) {
    std::string encoded, read_error;
    const auto result = settings_read_string(kNamespace, kPlanKey, kPlanBytes, encoded, read_error);
    if (result == SettingsReadResult::Found) {
      JsonDocument document;
      if (deserializeJson(document, encoded) || !document["elf"].is<const char *>() ||
          !document["local"].is<bool>()) last_error = "installation_saved_plan_invalid";
      else if (document["elf"].as<std::string>() == identity) {
        desired_valid = install_plan.parse(document["assets"], desired);
        local_plan = desired_valid && document["local"].as<bool>();
        if (!desired_valid) last_error = "installation_saved_plan_invalid";
      }
    } else if (result == SettingsReadResult::Failed) last_error = "installation_plan_read_failed";
  }
  return active.load();
}
static bool complete_installation(std::string &error,
    const std::array<DesiredResource, kManagedAssetCapacity> *expected, uint64_t generation) {
  if (policy.existing_assets_only) { error = "installation_disabled_boot_validation"; return false; }
  error.clear();
  // Lost acknowledgments may be retried while our activation is already
  // queued. A committed installation needs no new resource/OTA mutation.
  if (completed) return true;
  const auto ota = ota_service_status_snapshot();
  if (ota.pending_verify || !ota.boot_confirmed || ota.upload_active ||
      ota.pull_queued || ota.pull_active || ota.restart_pending) {
    error = "installation_firmware_not_ready"; return false;
  }
  if (!active) { error = "installation_not_active"; return false; }
  // This is an explicit completion or a trusted-plan installation. Finish
  // interrupted publication metadata/import before interpreting ready status.
  configure_managed_asset_startup_recovery(true);
  poll_managed_asset_recovery();
  const auto catalog = managed_assets_inventory_snapshot();
  for (const auto &asset : catalog.assets)
    if (asset.downloading) { error = "installation_resource_download_active"; return false; }
  // Read the actual persisted bytes through the product's digest, format and
  // model-family checks. Metadata ready alone does not complete installation.
  std::string failed;
  if(!validate_managed_assets(kResources.data(),resource_count,failed,error))return false;
  std::unique_lock<std::mutex> manifest_lock(desired_mutex, std::defer_lock);
  if (expected) {
    manifest_lock.lock();
    if (!desired_valid || desired_generation != generation ||
        !install_plan.matches(managed_assets_inventory_snapshot(), *expected)) {
      error = "installation_manifest_changed"; return false;
    }
  }
  const auto latest = ota_service_status_snapshot();
  if (latest.pending_verify || !latest.boot_confirmed || latest.upload_active ||
      latest.pull_queued || latest.pull_active || latest.restart_pending) {
    error = "installation_firmware_not_ready"; return false;
  }
  if (!settings_erase(kNamespace, kKey, error)) return false;
  completed = true;
  return true;
}
bool complete_firmware_installation(std::string &error) {
  return complete_installation(error, nullptr, 0);
}
void firmware_installation_accept_cloud_assets(JsonVariantConst assets) {
  if (policy.existing_assets_only || !policy.transaction_required) return;
  // The server returns a full catalog; a transient unavailable projection is
  // empty. Keep the last authenticated target through that temporary gap.
  if (assets.isNull() || (assets.is<JsonArrayConst>() && assets.as<JsonArrayConst>().size() == 0)) return;
  if (!active || completed) return;
  std::string error;
  if (!accept_plan(assets, false, error)) {
    std::lock_guard lock(desired_mutex); last_error = error;
  }
}
void poll_firmware_installation() {
  if (policy.existing_assets_only) return;
  if (!active) return;
  const auto now = esp_timer_get_time();
  if (now < next_completion_check) return;
  next_completion_check = now + 5000000;
  std::string error;
  if (cloud_restart_pending) {
    const bool accepted = ota_service_try_request_restart(error);
    std::lock_guard lock(desired_mutex); phase = "activating"; last_error = accepted ? "" : error; return;
  }
  if (completed) return;
  if (!firmware_ready()) {
    std::lock_guard lock(desired_mutex); phase = "waiting_firmware"; return;
  }
  std::array<DesiredResource, kManagedAssetCapacity> expected;
  uint64_t generation;
  { std::lock_guard lock(desired_mutex); if (!desired_valid) { phase = "waiting_manifest"; return; }
    expected = desired; generation = desired_generation; }
  // Firmware is confirmed and a trusted plan has been retained for this ELF.
  // Rescue/waiting_manifest never replays work, but this admitted installation
  // must finish a prior rename/import interrupted by power loss.
  configure_managed_asset_startup_recovery(true);
  poll_managed_asset_recovery();
  const auto inventory = managed_assets_inventory_snapshot();
  if (!install_plan.matches(inventory, expected)) {
    std::lock_guard lock(desired_mutex);
    if (generation != desired_generation) return;
    phase = "installing_resources"; last_error.clear();
    for (size_t i = 0; i < resource_count; ++i) {
      const auto asset=install_plan.observe(i,inventory,expected);
      if(asset.ready||asset.downloading)continue;
      if(!request_managed_asset(install_plan.request(i,expected),error))
        last_error = error;
    }
    return;
  }
  { std::lock_guard lock(desired_mutex); phase = "validating_resources"; }
  if (complete_installation(error, &expected, generation)) {
    cloud_restart_pending = true;
    const bool accepted = ota_service_try_request_restart(error);
    std::lock_guard lock(desired_mutex); phase = "activating"; last_error = accepted ? "" : error;
  } else {
    std::lock_guard lock(desired_mutex); last_error = error;
  }
}
ProductDeclarationNode firmware_installation_declaration(DeviceProjectionRegistry &registry) {
  return {policy.endpoint_id, [](void *context, std::string &error) {
    return static_cast<DeviceProjectionRegistry *>(context)->register_projection(
        {&capability, &projection, nullptr, true}, &error);
  }, &registry};
}
} // namespace installation
bool configure_firmware_installation(FirmwareInstallationPolicy policy,std::string &error){
  if(installation::configured){error="installation_already_configured";return false;}
  if(!policy.settings_namespace||!*policy.settings_namespace||!policy.endpoint_id||!*policy.endpoint_id||
      policy.required_assets.size()>kManagedAssetCapacity||(policy.transaction_required&&policy.required_assets.empty())){
    error="installation_policy_invalid";return false;
  }
  if(installation::active.load()){error="installation_already_active";return false;}
  const auto safe_id=[](const char *s,size_t limit){size_t n=0;for(;s[n];++n)if(n>=limit||!((s[n]>='a'&&s[n]<='z')||(s[n]>='0'&&s[n]<='9')||s[n]=='_'))return false;return n>0;};
  if(!safe_id(policy.settings_namespace,15)||!safe_id(policy.endpoint_id,63)){error="installation_policy_identity_invalid";return false;}
  for(size_t i=0;i<policy.required_assets.size();++i){
    if(!policy.required_assets[i]||!*policy.required_assets[i]){error="installation_asset_id_invalid";return false;}
    for(size_t j=0;j<i;++j)if(std::string_view(policy.required_assets[i])==policy.required_assets[j]){
      error="installation_asset_duplicate";return false;
    }
  }
  installation::policy=policy;installation::kNamespace=policy.settings_namespace;
  installation::resource_count=policy.required_assets.size();installation::kResources={};
  std::copy(policy.required_assets.begin(),policy.required_assets.end(),installation::kResources.begin());
  installation::install_plan=ManagedAssetInstallPlan<kManagedAssetCapacity>{installation::kResources,installation::resource_count};
  installation::capability.id=policy.endpoint_id;installation::module.id=policy.endpoint_id;
  installation::projection.capability_id=policy.endpoint_id;
  installation::configured=true;
  error.clear();return true;
}
bool firmware_installation_pending(){return installation::firmware_installation_pending();}
bool validate_firmware_installation_assets(std::string &error){return installation::validate_firmware_installation_assets(error);}
void configure_firmware_installation_recovery(bool enabled){installation::configure_firmware_installation_recovery(enabled);}
bool resume_firmware_installation(std::string &error){return installation::resume_firmware_installation(error);}
bool begin_firmware_installation(std::string &error){return installation::begin_firmware_installation(error);}
bool complete_firmware_installation(std::string &error){return installation::complete_firmware_installation(error);}
void firmware_installation_accept_cloud_assets(JsonVariantConst assets){installation::firmware_installation_accept_cloud_assets(assets);}
void poll_firmware_installation(){installation::poll_firmware_installation();}
ProductDeclarationNode firmware_installation_declaration(DeviceProjectionRegistry &registry){return installation::firmware_installation_declaration(registry);}
}
