#include "lunaredu_lunaros/model/device_model.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_host.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "persistence/storage/littlefs_list.h"
#include "persistence/storage/littlefs_path.h"
#include "persistence/storage/littlefs_read.h"
#include "persistence/storage/littlefs_volume.h"
#include "persistence/storage/littlefs_write.h"

namespace lunaredu::lunaros {

namespace {

const DeviceCapabilityDescriptor kLittleFsCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "littlefs_service";
  return descriptor;
}();

void sync_storage_capability(bool ready) {
  const bool updated = ready
                           ? activate_device_capability(
                                 storage_service_capability_descriptor())
                           : deactivate_device_capability(
                                 storage_service_capability_descriptor().id);
  if (!updated && ready) {
    LOGW("system.storage", "failed to register capability id=%s",
         kLittleFsCapability.id);
  }
}

} // namespace

const DeviceCapabilityDescriptor &storage_service_capability_descriptor() {
  return kLittleFsCapability;
}

bool setup_storage_runtime() {
  const bool ready = setup_littlefs_store();
  sync_storage_capability(ready);
  return ready;
}

namespace {

ServiceStartResult start_storage_service(void *) {
  return setup_storage_runtime()
             ? ServiceStartResult{ServiceStartDisposition::Ready,
                                  "filesystem_ready"}
             : ServiceStartResult{ServiceStartDisposition::Failed,
                                  storage_last_error()};
}

} // namespace

ServiceNode storage_service_node() {
  static constexpr ServiceDependency kDependencies[] = {
      {kSystemRuntimeServiceId, true},
  };
  return {
      {kStorageServiceId, "Persistent Storage", kMainServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_storage_service,
  };
}

namespace {

bool format_storage_runtime() {
  const bool ready = littlefs_store_format();
  sync_storage_capability(ready);
  return ready;
}

} // namespace

bool storage_ready() { return littlefs_store_ready(); }

const char *storage_mount_path() { return littlefs_store_mount_path(); }

const char *storage_partition_label() {
  return littlefs_store_partition_label();
}

size_t storage_total_bytes() { return littlefs_store_total_bytes(); }

size_t storage_used_bytes() { return littlefs_store_used_bytes(); }

bool storage_refresh_usage() { return littlefs_store_refresh_usage(); }

bool storage_format() { return format_storage_runtime(); }

const char *storage_last_error() { return littlefs_store_last_error(); }

std::string storage_normalize_path(const char *path) {
  return littlefs_store_normalize_path(path);
}

bool storage_is_safe_path(const char *path) {
  return littlefs_store_is_safe_path(path);
}

bool storage_path_exists(const char *path) {
  return littlefs_store_path_exists(path);
}

bool storage_ensure_directory(const char *path) {
  return littlefs_store_ensure_directory(path);
}

bool storage_read_text_file(const char *path, std::string &content) {
  return littlefs_store_read_text_file(path, content);
}

bool storage_read_file(const char *path, std::vector<uint8_t> &content) {
  return littlefs_store_read_file(path, content);
}
bool storage_read_range(const char *path, size_t offset, uint8_t *data, size_t size) {
  return littlefs_store_read_range(path, offset, data, size);
}

bool storage_stream_file(const char *path, StorageFileChunkWriter writer,
                         void *context, std::string *error) {
  return littlefs_store_stream_file(path, writer, context, error);
}

bool storage_write_text_file(const char *path, const char *content) {
  return littlefs_store_write_text_file(path, content);
}

bool storage_write_text_file_ex(const char *path, const char *content,
                                size_t *written_bytes) {
  return littlefs_store_write_text_file_ex(path, content, written_bytes);
}

bool storage_write_file(const char *path, const uint8_t *data, size_t size,
                        size_t *written_bytes) {
  return littlefs_store_write_file(path, data, size, written_bytes);
}

bool storage_write_streamed_file(const char *path,
                                 StorageWriteStreamProducer producer,
                                 void *producer_context,
                                 size_t *written_bytes,
                                 std::string *error) {
  return littlefs_store_write_streamed_file(
      path, producer, producer_context, written_bytes, error);
}

bool storage_delete_file(const char *path) {
  return littlefs_store_delete_file(path);
}

bool storage_move_file(const char *source_path, const char *destination_path,
                       std::string *error) {
  return littlefs_store_move_file(source_path, destination_path, error);
}

bool storage_list_entries(const char *path, StorageEntryVisitor visitor,
                          void *context) {
  return littlefs_store_list_entries(path, visitor, context);
}

bool storage_list_entries_recursive(const char *path, uint8_t max_depth,
                                    StorageEntryVisitor visitor,
                                    void *context) {
  return littlefs_store_list_entries_recursive(path, max_depth, visitor,
                                               context);
}

} // namespace lunaredu::lunaros
