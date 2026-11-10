#pragma once
#include "lunaros_storage_service_id.h"

// Public storage facade. The current ESP32-S3 implementation uses LittleFS,
// while callers depend only on this contract.

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>


namespace lunaredu::lunaros {
class DeviceCapabilityRegistry;
struct DeviceCapabilityDescriptor;

struct ServiceNode;
ServiceNode storage_service_node(DeviceCapabilityRegistry &capabilities);

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


bool storage_ready();
// Idempotent early mount for persistent boot diagnostics. Never formats a volume.
bool storage_initialize();
const char *storage_mount_path();
const char *storage_partition_label();
size_t storage_total_bytes();
size_t storage_used_bytes();
bool storage_refresh_usage();
bool storage_format(DeviceCapabilityRegistry &capabilities);
const char *storage_last_error();

std::string storage_normalize_path(const char *path);
bool storage_is_safe_path(const char *path);
// Canonical user-content path; excludes private OS/resource roots.
bool storage_user_file_path_allowed(const char *path);
bool storage_path_exists(const char *path);
bool storage_ensure_directory(const char *path);

bool storage_read_text_file(const char *path, std::string &content);
bool storage_read_file(const char *path, std::vector<uint8_t> &content);
// Check the opened file's size before allocating its contents, under the same
// filesystem lock as the read. Failure clears content; zero permits empty files.
bool storage_read_text_file_bounded(const char *path, size_t maximum_bytes, std::string &content);
bool storage_read_file_bounded(const char *path, size_t maximum_bytes, std::vector<uint8_t> &content);
bool storage_read_range(const char *path, size_t offset, uint8_t *data, size_t size);
// Stream callbacks run outside the filesystem mutex. They are synchronous:
// callback buffers/context must not be retained after the callback returns.
// An open stream pins its path against overwrite/delete/move/format; other
// files remain accessible. Conflicting operations report file_busy.
bool storage_stream_file(const char *path, StorageFileChunkWriter writer,
                         void *context, std::string *error = nullptr);
// Pin an existing regular file against mutation for a synchronous operation.
// Callback runs outside the filesystem mutex and may use read_range().
using StoragePinnedFileOperation=bool (*)(size_t file_bytes,void *,std::string &);
bool storage_with_pinned_file(const char *,StoragePinnedFileOperation,void *,std::string &error);

// Writes publish a completed temporary file atomically. Failure preserves the
// previous destination; written_bytes reports only successfully published data.
bool storage_write_text_file(const char *path, const char *content,
                                size_t *written_bytes = nullptr);
bool storage_write_file(const char *path, const uint8_t *data, size_t size,
                        size_t *written_bytes = nullptr);
bool storage_write_streamed_file(
    const char *path, StorageWriteStreamProducer producer,
    void *producer_context, size_t *written_bytes = nullptr,
    std::string *error = nullptr);
// A known-length producer reserves its complete capacity once. Too few/many
// bytes reject publication; concurrent writers cannot borrow the reservation.
bool storage_write_sized_streamed_file(
    const char *path, size_t expected_bytes, StorageWriteStreamProducer producer,
    void *producer_context, size_t *written_bytes = nullptr,
    std::string *error = nullptr);
// Incremental atomic publication on an existing I/O owner. A live handle pins
// the destination against all other mutations and format. Append is strictly
// sequential, <=4096 bytes per call; commit requires exactly expected_bytes.
// No mutex is held between calls. Not resumable after process/device restart.
struct StorageWriteHandle {
  uint64_t id=0;
  explicit operator bool() const {return id!=0;}
};
bool storage_begin_write(const char *path,size_t expected_bytes,StorageWriteHandle &,std::string &error);
bool storage_append_write(StorageWriteHandle,size_t offset,const uint8_t *,size_t,std::string &error);
bool storage_commit_write(StorageWriteHandle &,std::string &error);
bool storage_abort_write(StorageWriteHandle &,std::string &error);
bool storage_delete_file(const char *path);
bool storage_move_file(const char *source_path, const char *destination_path,
                       std::string *error = nullptr);

bool storage_list_entries(const char *path, StorageEntryVisitor visitor,
                          void *context);
bool storage_list_entries_recursive(const char *path, uint8_t max_depth,
                                    StorageEntryVisitor visitor,
                                    void *context);

} // namespace lunaredu::lunaros
