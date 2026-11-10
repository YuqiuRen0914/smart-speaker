#include "lunaros_ota_lifecycle.h"
#include "device_api/device_api_context.h"
#include "lunaros_platform_link_json.h"
#include "platform_link_operations.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <ctime>
#include <mutex>
#include "platform_link_ota_retry.h"
#include "platform_link_cloud_ota.h"

#include <esp_log.h>
#include "platform/system.h"
#include "system/identity.h"
#include "lunaredu_ota.h"
#include "lunaros_resource_managed_assets.h"
#include "platform_link_http.h"
#include "platform_link_scheduler.h"
#include "platform_link_state.h"
#include "device_api/device_projection.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kTag = "platform_link";
AutomaticOtaRetryGuard g_auto_ota_retry;
std::mutex g_auto_ota_mutex;
std::string g_pending_auto_ota_key;
std::string g_available_assignment;
// Start with a lightweight revision handshake. The server requests the full
// declaration only if its persisted copy is missing or stale.
bool g_publish_workbench = false;
std::string ota_identity(){
  const auto account=account_identity();
  return account.empty()?std::string(hardware_serial_number())+":"+std::to_string(pairing_snapshot().binding_epoch):account;
}

const char *remote_access_name(DeviceRemoteAccess access) {
  switch (access) {
  case DeviceRemoteAccess::Query: return "query";
  case DeviceRemoteAccess::Command: return "command";
  default: return "none";
  }
}

void add_workbench_manifest(JsonObject manifest) {
  DeviceApiContext *device = runtime_device();
  if (device == nullptr || device->projections() == nullptr) return;
  auto &registry = *device->projections();
  manifest["version"] = 1;
  manifest["revision"] = registry.declaration_revision();
  JsonArray modules = manifest["modules"].to<JsonArray>();
  struct Context { DeviceProjectionRegistry &registry; JsonArray modules; } context{registry, modules};
  device->model.models.visit([](const DeviceModuleSpec &spec, void *opaque) {
    auto &context = *static_cast<Context *>(opaque);
    if (spec.id == nullptr || spec.capability_id == nullptr) return;
    JsonObject module = context.modules.add<JsonObject>();
    module["id"] = spec.id;
    module["kind"] = spec.kind != nullptr ? spec.kind : spec.id;
    module["capability_id"] = spec.capability_id;
    context.registry.visit_modules([](const DeviceModuleProjection &projection, void *opaque) {
      auto &target = *static_cast<JsonObject *>(opaque);
      if (projection.module == nullptr || projection.module->id == nullptr ||
          std::string(projection.module->id) != target["id"].as<std::string>()) return;
      if (projection.module->label != nullptr) target["label"] = projection.module->label;
      if (projection.module->fill_contract != nullptr)
        projection.module->fill_contract(target["contract"].to<JsonObject>());
      if (projection.module->workbench_ui != nullptr)
        target["ui"] = serialized(projection.module->workbench_ui);
    }, &module);
    JsonArray operations = module["operations"].to<JsonArray>();
    for (size_t i = 0; i < spec.operation_count; ++i) {
      const DeviceOperationSpec &op = spec.operations[i];
      if (op.id == nullptr) continue;
      const DeviceOperationProjection *binding = context.registry.find_operation(spec.id, op.id);
      if (binding == nullptr) continue;
      JsonObject item = operations.add<JsonObject>();
      item["id"] = op.id;
      item["mutating"] = op.mutating;
      item["cancellable"] = op.cancellable;
      if (!module["ui"].isNull()) {
        if (op.request_schema) item["input_schema"] = serialized(op.request_schema);
        if (op.response_schema) item["output_schema"] = serialized(op.response_schema);
      }
      item["remote"] = remote_access_name(op.remote_access);
      item["handler"] = binding->handler_kind == DeviceOperationHandlerKind::JsonQuery ? "json_query"
          : binding->handler_kind == DeviceOperationHandlerKind::JsonCommand ? "json_command"
          : binding->handler_kind == DeviceOperationHandlerKind::Media ? "media"
          : binding->handler_kind == DeviceOperationHandlerKind::Upload ? "upload" : "bulk";
      item["method"] = binding->method == DeviceOperationMethod::Get ? "GET"
          : binding->method == DeviceOperationMethod::Post ? "POST"
          : binding->method == DeviceOperationMethod::Put ? "PUT" : "DELETE";
      item["path"] = std::string("/api/v1/modules/") + spec.id + "/" + op.id;
      item["response"] = op.kind == DeviceOperationKind::Stream ? "stream" : "json";
    }
  }, &context);
}

void cache_available_firmware(JsonVariantConst firmware_update) {
  g_available_assignment = firmware_update["assignment_id"] | "";
  OtaAvailableUpdate available_update;
  if (runtime_config().firmware_updates_enabled &&
      !firmware_update.isNull() && (firmware_update["has_update"] | false)) {
    const JsonVariantConst release = firmware_update["release"];
    const JsonVariantConst ui = release["ota_manifest"]["ui"];
    const std::string policy = firmware_update["policy"] | "";
    available_update.available = true;
    available_update.mandatory =
        policy == "mandatory" || (ui["mandatory"] | false);
    available_update.version =
        firmware_update["latest_version"] | "";
    available_update.summary = ui["summary"] | "";
    available_update.changelog = ui["changelog"] | "";
    available_update.download_url = firmware_update["download_url"] | "";
    available_update.package_sha256 = firmware_update["package_sha256"] | "";
    available_update.package_size =
        firmware_update["package_size"] | static_cast<size_t>(0);
    if (available_update.download_url.empty() ||
        available_update.package_sha256.empty() ||
        available_update.package_size == 0) {
      available_update.available = false;
    }
    const unsigned protocol=firmware_update["protocol_version"]|0U;
    if(protocol!=0){std::string error;
      if(protocol!=2||!decode_cloud_role_offer(firmware_update,available_update,error))available_update.available=false;
    }else if(runtime_config().role_firmware_updates_only)available_update.available=false;
  }
  ota_service_set_available_update(available_update);
}

void accept_notifications(JsonVariantConst notifications) {
  const auto *observer = runtime_config().observer;
  if (!observer || !observer->notification || !notifications.is<JsonArrayConst>())
    return;
  JsonDocument receipt;
  JsonArray accepted = receipt["ids"].to<JsonArray>();
  unsigned count = 0;
  for (JsonVariantConst item : notifications.as<JsonArrayConst>()) {
    if (++count > 4) break;
    PlatformLinkNotification notice;
    notice.id = item["id"] | "";
    notice.kind = item["kind"] | "";
    notice.priority = item["priority"] | "normal";
    notice.title = item["title"] | "";
    notice.body = item["body"] | "";
    notice.action_kind = item["actionKind"] | "";
    notice.action_ref = item["actionRef"] | "";
    if (!notice.id.empty() && !notice.kind.empty() &&
        observer->notification(notice)) accepted.add(notice.id);
  }
  if (accepted.size() == 0) return;
  std::string body;
  serializeJson(receipt, body);
  int status_code = 0;
  if (!perform_request("POST", "/api/link/device-runtime/notifications/ack",
                       &body, nullptr, &status_code)) {
    ESP_LOGW(kTag, "notification receipt failed status=%d; server will replay",
         status_code);
  }
}

void apply_managed_assets(JsonVariantConst assets) {
  if (!runtime_config().managed_assets_enabled) return;
  if (!assets.is<JsonArrayConst>()) return;
  if (assets.as<JsonArrayConst>().size() == 0) return;
  const auto ota=ota_service_status_snapshot();
  // A normal business presence may offer new firmware with its new model.
  // Preserve current resources until that image is installed. Product-owned
  // installers observe the manifest separately after confirming their image.
  if (ota_service_available_update_snapshot().available || firmware_update_pending() ||
      !ota.boot_confirmed || ota.pending_verify || ota.upload_active || ota.pull_queued ||
      ota.pull_active || ota.restart_pending) return;
  const auto catalog = managed_assets_inventory_snapshot();
  if (!catalog.pool_enabled || catalog.asset_count == 0) return;
  for (JsonVariantConst item : assets.as<JsonArrayConst>()) {
    ManagedAssetRequest request;
    request.id = item["id"] | "";
    request.revision = item["revision"] | "";
    request.url = item["url"] | "";
    request.sha256 = item["sha256"] | "";
    request.bytes = item["bytes"] | static_cast<size_t>(0);
    bool declared = false;
    bool current = false;
    for (size_t i = 0; i < catalog.asset_count; ++i) {
      const auto &installed = catalog.assets[i];
      if (installed.id != request.id) continue;
      declared = true;
      current = installed.revision == request.revision &&
          installed.bytes == request.bytes && installed.sha256 == request.sha256 &&
          installed.state == ManagedAssetState::Ready;
      break;
    }
    if (!declared || current) continue;
    std::string error;
    if (!request_managed_asset(request, error)) {
      ESP_LOGW(kTag, "managed asset %s rejected: %s", request.id.c_str(), error.c_str());
    }
  }
}

void schedule_automatic_update(JsonVariantConst firmware_update) {
  const OtaAvailableUpdate available = ota_service_available_update_snapshot();
  const OtaServiceStatus status = ota_service_status_snapshot();
  const std::string environment = std::string(platform_is_production()?"production:":"development:")+platform_base_url();
  const uint32_t now = monotonic_ms();
  if ((status.install_phase=="confirmed"||status.install_phase=="boot_updated") && status.boot_confirmed &&
      (status.running_image_state=="valid"||status.running_image_state=="factory") && !status.install_sha256.empty()) {
    std::lock_guard<std::mutex> lock(g_auto_ota_mutex);
    const auto context=automatic_ota_key(environment,ota_identity(),{},status.install_sha256);
    if (!g_auto_ota_retry.confirmed(context,now)&&!cloud_role_retry_repair_allowed(available)) return;
    if (!available.role_aware&&status.install_sha256==available.package_sha256) return;
  }
  if (!available.available) {
    return;
  }
  if(available.role_aware&&!cloud_role_offer_admitted(available))return;
  if (available.version.empty()) return;
  const auto &config = runtime_config();
  const std::string assignment_id = firmware_update["assignment_id"] | "";
  const bool assigned = !assignment_id.empty() &&
                        (firmware_update["auto_install"] | false);
  const bool optional_auto = !available.mandatory && !assigned &&
      config.auto_install_optional_firmware &&
      config.auto_install_optional_firmware(available);
  if (!available.mandatory && !assigned && !optional_auto) return;
  if (cloud_role_observe_installed(available)) return;
  if (status.upload_active || status.pull_queued || status.pull_active ||
      status.restart_pending || firmware_update_pending()) return;
  const std::string key = automatic_ota_key(environment,ota_identity(),
      assignment_id,available.package_sha256);
  std::lock_guard<std::mutex> lock(g_auto_ota_mutex);
  // Commit the reservation before queueing. Power loss cannot reset the
  // attempt budget, and failed storage never starts an automatic install.
  if (!reserve_cloud_role_retry(g_auto_ota_retry,available,key,static_cast<int64_t>(std::time(nullptr)),now)) return;
  g_pending_auto_ota_key=key;
  request_firmware_update();
}

} // namespace

void fill_automatic_ota_status(JsonObject target) {
  std::lock_guard<std::mutex> lock(g_auto_ota_mutex);
  g_auto_ota_retry.fill_status(target);
}

bool reset_automatic_ota_retry(std::string &error) {
  std::lock_guard<std::mutex> lock(g_auto_ota_mutex);
  if (!g_auto_ota_retry.reset()) {error=g_auto_ota_retry.error();return false;}
  g_pending_auto_ota_key.clear();error.clear();return true;
}

bool publish_presence(int *http_status) {
  if(http_status)*http_status=-1;
  configure_resource_transport();

  JsonDocument doc;
  doc["firmwareVersion"] = firmware_version();
  add_capabilities(doc["capabilities"].to<JsonArray>());
  add_runtime_state(doc["state"].to<JsonObject>());
  fill_cloud_role_state(doc["state"].as<JsonObject>());
  if (runtime_device() != nullptr && runtime_device()->projections() != nullptr)
    doc["state"]["workbench_revision"] = runtime_device()->projections()->declaration_revision();
  if (g_publish_workbench && runtime_device() != nullptr &&
      runtime_device()->projections() != nullptr)
    add_workbench_manifest(doc["workbench"].to<JsonObject>());
  if (!doc["workbench"].isNull() && measureJson(doc["workbench"]) > 49'152) {
    const JsonArray modules = doc["workbench"]["modules"].as<JsonArray>();
    for (JsonObject module : modules)
      module.remove("contract");
    if (measureJson(doc["workbench"]) > 49'152) {
      doc.as<JsonObject>().remove("workbench");
      ESP_LOGW(kTag, "workbench manifest exceeds PlatformLink limit");
    }
  }
  std::string body;
  serializeJson(doc, body);

  JsonDocument response;
  int status_code = 0;
  if (!perform_request("POST", "/api/link/device-runtime/presence", &body,
                       &response, &status_code)) {
    if(http_status)*http_status=status_code;
    if (status_code == 401 &&
        pairing_snapshot().state != PlatformLinkBindingState::Pairing) {
      update_pairing_snapshot(PlatformLinkBindingState::Error, {}, {}, 0,
                              "device_credential_rejected");
    }
    return false;
  }
  if(http_status)*http_status=status_code;
  g_publish_workbench = response["data"]["needWorkbenchManifest"] | false;
  const int binding_epoch = response["data"]["bindingEpoch"] | 0;
  const std::string device_status = response["data"]["deviceStatus"] | "";
  if (!response["data"]["bindingEpoch"].is<int>() || binding_epoch<0 ||
      state_from_server(device_status)==PlatformLinkBindingState::Error ||
      (device_status=="active" && (response["data"]["boundAccount"]["id"] | std::string{}).empty())) {
    update_pairing_snapshot(PlatformLinkBindingState::Error,{}, {},0,"platform_binding_response_invalid");
    return false;
  }
  if (device_status == "active") {
    const std::string account_id =
        response["data"]["boundAccount"]["id"] | "";
    const std::string account_display_name =
        response["data"]["boundAccount"]["displayName"] | "";
    const std::string account_avatar_url =
        response["data"]["boundAccount"]["avatarUrl"] | "";
    const bool account_can_manage_hardware =
        response["data"]["boundAccount"]["canManageHardware"] | false;
    update_pairing_snapshot(PlatformLinkBindingState::Bound, {}, {}, 0, {},
                            binding_epoch, account_id, account_display_name,
                            account_avatar_url, account_can_manage_hardware);
  } else {
    const PlatformLinkPairingSnapshot current = pairing_snapshot();
    if (current.state != PlatformLinkBindingState::Pairing) {
      update_pairing_snapshot(state_from_server(device_status), {}, {}, 0, {},
                              binding_epoch);
    }
  }

  update_shared_access(response["data"]["accessEpoch"] | -1,
                       response["data"]["sharedControlEnabled"] | false,
                       response["data"]["controlManagerId"] | "");

  accept_notifications(response["data"]["notifications"]);
  update_account_avatar_resource(pairing_snapshot(),
                                 response["data"]["resourceManifest"]);
  std::string commit_error;
  if (!persist_pairing_snapshot(commit_error)) return false;
  const bool profile_current = reconcile_product_profile_after_presence();
  if (profile_current) {
    const JsonVariantConst firmware_update = response["data"]["firmwareUpdate"];
    cache_available_firmware(firmware_update);
    schedule_automatic_update(firmware_update);
    apply_managed_assets(response["data"]["managedAssets"]);
  } else {
    cache_available_firmware(JsonVariantConst{});
    response["data"].remove("firmwareUpdate");
    response["data"].remove("managedAssets");
  }
  const auto *observer = runtime_config().observer;
  if (observer && observer->presence) {
    observer->presence(pairing_snapshot(), response["data"]);
  }
  ESP_LOGI(kTag, "account presence accepted: serial=%s binding_epoch=%d",
       hardware_serial_number(), binding_epoch);
  return true;
}

bool refresh_available_firmware(bool report_failure) {
  if (!runtime_config().firmware_updates_enabled) return false;
  if (!product_profile_current()) {
    cache_available_firmware(JsonVariantConst{});
    if (report_failure) ota_service_report_failure("platform_product_profile_pending");
    return false;
  }
  JsonDocument response;
  int status_code = 0;
  if (!perform_request("GET", "/api/link/device-runtime/firmware", nullptr,
                       &response, &status_code)) {
    if (report_failure) {
      char failure[64];
      std::snprintf(failure, sizeof(failure), "ota_ticket_refresh_failed:%d", status_code);
      ota_service_report_failure(failure);
    }
    ESP_LOGW(kTag, "firmware ticket refresh failed status=%d", status_code);
    return false;
  }
  if (!product_profile_current()) {
    cache_available_firmware(JsonVariantConst{});
    return false;
  }
  cache_available_firmware(response["data"]);
  return true;
}

bool refresh_and_apply_firmware_update() {
  if (!refresh_available_firmware(true)) return false;
  {
    std::lock_guard<std::mutex> lock(g_auto_ota_mutex);
    if (!g_pending_auto_ota_key.empty()) {
      const auto available=ota_service_available_update_snapshot();
      const std::string environment=std::string(platform_is_production()?"production:":"development:")+platform_base_url();
      const auto actual=automatic_ota_key(environment,ota_identity(),g_available_assignment,available.package_sha256);
      const auto reserved=std::move(g_pending_auto_ota_key);
      g_pending_auto_ota_key.clear();
      if (actual!=reserved) {
        ota_service_report_failure("auto_ota_assignment_changed");
        return false;
      }
    }
  }
  std::string error;
  const auto available=ota_service_available_update_snapshot();
  if(available.role_aware&&!prepare_cloud_role_install(available,error)){
    ota_service_report_failure(error.c_str());return false;
  }
  if (!ota_service_apply_available_update(error)) {
    if(available.role_aware)cloud_role_install_rejected(error);
    ESP_LOGW(kTag, "fresh official OTA update rejected error=%s", error.c_str());
    return false;
  }
  ESP_LOGI(kTag, "fresh official OTA ticket accepted");
  return true;
}

} // namespace lunaredu::lunaros::platform_link
