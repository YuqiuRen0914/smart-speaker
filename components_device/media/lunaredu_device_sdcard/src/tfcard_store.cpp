#include "lunaredu_device_sdcard_storage.h"
#include "lunaredu_device_sdcard_tfcard_store.h"

#include <mutex>
#include <string.h>

#include "bsp/lunaredu_bsp.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/device_api/device_projection_json.h"

namespace lunaredu::device {

using lunaredu::lunaros::DeviceCapabilityDescriptor;
using lunaredu::lunaros::DeviceMetadataProjection;
using lunaredu::lunaros::DeviceModuleProjection;
using lunaredu::lunaros::DeviceModuleProjectionMetadata;
using lunaredu::lunaros::DeviceOperationDefinition;
using lunaredu::lunaros::DeviceOperationResult;
using lunaredu::lunaros::activate_device_capability;
using lunaredu::lunaros::deactivate_device_capability;
using lunaredu::lunaros::device_operation_ack;
using lunaredu::lunaros::device_operation_error;
using lunaredu::lunaros::make_device_json_command;
using lunaredu::lunaros::make_device_json_provider;
using lunaredu::lunaros::register_device_projection;

namespace {

const DeviceCapabilityDescriptor kTfCardCapability = {
    "tfcard_store",
    nullptr,
    0,
};

constexpr const char *kTfCardMountPath = "/sdcard";
std::recursive_mutex g_tfcard_mutex;
bool g_tfcard_setup = false;
bool g_tfcard_local_api_registered = false;

void fill_tfcard_store_payload(JsonObject payload);

void fill_tfcard_current_status_payload(JsonObject payload) {
  fill_tfcard_store_payload(payload);
}

void sync_tfcard_active_capability() {
  if (bsp_sdcard_get_status().mounted) {
    activate_device_capability(kTfCardCapability);
    return;
  }
  deactivate_device_capability(kTfCardCapability.id);
}

DeviceOperationResult handle_tfcard_mount(
    JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;
  if (!tfcard_store_mount()) {
    fill_tfcard_store_payload(response_payload);
    return device_operation_error("tfcard_mount_failed", 500,
                                     "tfcard_request_failed");
  }

  fill_tfcard_store_payload(response_payload);
  return device_operation_ack();
}

DeviceOperationResult handle_tfcard_unmount(
    JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;
  if (!tfcard_store_unmount()) {
    fill_tfcard_store_payload(response_payload);
    return device_operation_error("tfcard_unmount_failed", 500,
                                     "tfcard_request_failed");
  }

  fill_tfcard_store_payload(response_payload);
  return device_operation_ack();
}

constexpr DeviceOperationDefinition kTfCardOperations[] = {
    make_device_json_provider("status.get", fill_tfcard_current_status_payload),
    make_device_json_command("mount", handle_tfcard_mount),
    make_device_json_command("unmount", handle_tfcard_unmount),
};

const DeviceModuleProjectionMetadata kTfCardLocalApiMetadata = {
    "tfcard",
    "tfcard",
};

const DeviceModuleProjection kTfCardLocalApi = {
    "tfcard_store",
    &kTfCardLocalApiMetadata,
    kTfCardOperations,
    sizeof(kTfCardOperations) / sizeof(kTfCardOperations[0]),
};

const DeviceMetadataProjection kTfCardMetadata = {
    "tfcard",
    "tfcard",
    fill_tfcard_current_status_payload,
    nullptr,
};

bool setup_tfcard_local_api() {
  if (g_tfcard_local_api_registered) {
    return true;
  }

  g_tfcard_local_api_registered = register_device_projection(
      {&kTfCardCapability, &kTfCardLocalApi, &kTfCardMetadata, false});
  if (!g_tfcard_local_api_registered) {
    LOGW("device.tfcard", "device contract registration failed");
  }
  return g_tfcard_local_api_registered;
}

} // 匿名命名空间

const DeviceCapabilityDescriptor &tfcard_store_capability_descriptor() {
  return kTfCardCapability;
}

bool setup_tfcard_store(bool mount_now) {
  std::lock_guard<std::recursive_mutex> lock(g_tfcard_mutex);
  g_tfcard_setup = true;
  setup_tfcard_local_api();
  if (!mount_now) {
    sync_tfcard_active_capability();
    return true;
  }
  return tfcard_store_mount();
}

void tfcard_store_deinit() {
  std::lock_guard<std::recursive_mutex> lock(g_tfcard_mutex);
  const esp_err_t result = bsp_sdcard_unmount();
  if (result != ESP_OK) {
    LOGW("device.tfcard", "tf card deinit unmount failed: %s",
         esp_err_to_name(result));
  } else {
    g_tfcard_setup = false;
  }
  sync_tfcard_active_capability();
}

bool tfcard_store_mount() {
  std::lock_guard<std::recursive_mutex> lock(g_tfcard_mutex);
  g_tfcard_setup = true;
  setup_tfcard_local_api();
  const esp_err_t result = bsp_sdcard_mount(kTfCardMountPath);
  if (result != ESP_OK) {
    sync_tfcard_active_capability();
    LOGW("device.tfcard", "tf card mount failed: %s", esp_err_to_name(result));
    return false;
  }
  sync_tfcard_active_capability();
  LOGI("device.tfcard", "tf card mounted path=%s", kTfCardMountPath);
  return true;
}

bool tfcard_store_unmount() {
  std::lock_guard<std::recursive_mutex> lock(g_tfcard_mutex);
  const esp_err_t result = bsp_sdcard_unmount();
  if (result != ESP_OK) {
    LOGW("device.tfcard", "tf card unmount failed: %s", esp_err_to_name(result));
    return false;
  }
  sync_tfcard_active_capability();
  return true;
}

bool tfcard_store_setup_done() {
  std::lock_guard<std::recursive_mutex> lock(g_tfcard_mutex);
  return g_tfcard_setup;
}

bool tfcard_store_ready() {
  std::lock_guard<std::recursive_mutex> lock(g_tfcard_mutex);
  return bsp_sdcard_get_status().mounted;
}

TfCardStoreStatus tfcard_store_status() {
  std::lock_guard<std::recursive_mutex> lock(g_tfcard_mutex);
  const bsp_sdcard_status_t status = bsp_sdcard_get_status();
  return {
      .mounted = status.mounted,
      .mount_path = status.mount_path == nullptr ? kTfCardMountPath
                                                 : status.mount_path,
      .capacity_bytes = status.capacity_bytes,
      .last_error = status.last_error == nullptr ? "" : status.last_error,
  };
}

bool tfcard_resolve_path(const char *path, std::string &host_path) {
  std::lock_guard<std::recursive_mutex> lock(g_tfcard_mutex);
  const TfCardStoreStatus status = tfcard_store_status();
  if (!status.mounted || path == nullptr || path[0] != '/') {
    return false;
  }
  if (strcmp(path, "/") == 0) {
    host_path = status.mount_path;
    return true;
  }
  host_path = status.mount_path + path;
  return true;
}

namespace {

void fill_tfcard_store_payload(JsonObject payload) {
  const TfCardStoreStatus status = tfcard_store_status();
  payload["ready"] = status.mounted;
  payload["mounted"] = status.mounted;
  payload["type"] = "fatfs";
  payload["mount_path"] = status.mount_path;
  payload["capacity_bytes"] = status.capacity_bytes;
  payload["last_error"] = status.last_error;
  payload["setup"] = tfcard_store_setup_done();
}

} // namespace

} // namespace lunaredu::device
