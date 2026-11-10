#pragma once

// Public storage facade. The current ESP32-S3 implementation uses LittleFS,
// while callers depend only on this contract.

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "lunaredu_lunaros/model/device_model.h"

namespace lunaredu::lunaros {

struct ServiceNode;
ServiceNode storage_service_node();

using StorageFileChunkWriter =
    bool (*)(const uint8_t *data, size_t size, void *context);
using StorageWriteStreamProducer =
    bool (*)(StorageFileChunkWriter writer, void *writer_context,
             void *producer_context, std::string &error);

struct StorageListEntry {
  std::string path;
  size_t size_bytes = 0;
  bool is_directory = false;
  uint64_t modified_ms = 0;
};

using StorageEntryVisitor = bool (*)(const StorageListEntry &entry,
                                     void *context);

// Mounts and validates the single ESP32-S3 persistent volume. Product
// composition owns when this is called; consumers must never initialize
// storage as a side effect of an operation.
bool setup_storage_runtime();
const DeviceCapabilityDescriptor &storage_service_capability_descriptor();
bool storage_ready();
const char *storage_mount_path();
const char *storage_partition_label();
size_t storage_total_bytes();
size_t storage_used_bytes();
bool storage_refresh_usage();
bool storage_format();
const char *storage_last_error();

std::string storage_normalize_path(const char *path);
bool storage_is_safe_path(const char *path);
bool storage_path_exists(const char *path);
bool storage_ensure_directory(const char *path);

bool storage_read_text_file(const char *path, std::string &content);
bool storage_read_file(const char *path, std::vector<uint8_t> &content);
bool storage_read_range(const char *path, size_t offset, uint8_t *data, size_t size);
// Stream callbacks run outside the filesystem mutex. They are synchronous:
// callback buffers/context must not be retained after the callback returns.
// An open stream pins its path against overwrite/delete/move/format; other
// files remain accessible. Conflicting operations report file_busy.
bool storage_stream_file(const char *path, StorageFileChunkWriter writer,
                         void *context, std::string *error = nullptr);

// Writes publish a completed temporary file atomically. Failure preserves the
// previous destination; written_bytes reports only successfully published data.
bool storage_write_text_file(const char *path, const char *content);
bool storage_write_text_file_ex(const char *path, const char *content,
                                size_t *written_bytes);
bool storage_write_file(const char *path, const uint8_t *data, size_t size,
                        size_t *written_bytes = nullptr);
bool storage_write_streamed_file(
    const char *path, StorageWriteStreamProducer producer,
    void *producer_context, size_t *written_bytes = nullptr,
    std::string *error = nullptr);
bool storage_delete_file(const char *path);
bool storage_move_file(const char *source_path, const char *destination_path,
                       std::string *error = nullptr);

bool storage_list_entries(const char *path, StorageEntryVisitor visitor,
                          void *context);
bool storage_list_entries_recursive(const char *path, uint8_t max_depth,
                                    StorageEntryVisitor visitor,
                                    void *context);

} // namespace lunaredu::lunaros
