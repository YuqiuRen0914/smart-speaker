#include "device_api/device_api_context.h"
#include "lunaros_storage_device_operations.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>

#include "device_api/device_operation_types.h"
#include "lunaros_storage.h"
#include "lunaros_storage_private_paths.h"
#include "lunaros_storage_pool.h"

namespace lunaredu::lunaros {

namespace {

constexpr size_t kLittleFsMaxReadPayloadBytes = 8192;
constexpr size_t kLittleFsMaxWritePayloadBytes = 8192;
constexpr size_t kLittleFsUploadChunkBytes = 1024;
constexpr size_t kLittleFsListMaximumItems = 64;
constexpr size_t kLittleFsListMaximumJsonBytes = 8192;
constexpr size_t kLittleFsListMaximumPathBytes = 255;

struct BulkFsListContext {
  JsonArray *items = nullptr;
  size_t offset = 0;
  size_t limit = kLittleFsListMaximumItems;
  size_t visited = 0;
  size_t count = 0;
  size_t encoded_bytes = 0;
  bool more = false;
  const char *error = nullptr;
};

struct BulkFsUploadContext {
  const DeviceBulkRequest *request = nullptr;
  size_t remaining = 0;
};

struct MediaFileWriterContext {
  const DeviceMediaWriter *writer = nullptr;
};

struct TextReadContext {
  std::string *content = nullptr;
  size_t max_bytes = 0;
  size_t bytes_read = 0;
  bool overflow = false;
};

bool request_has_arg(const DeviceBulkRequest &request, const char *name) {
  return request.has_arg != nullptr &&
         request.has_arg(name, request.context);
}

std::string request_arg(const DeviceBulkRequest &request, const char *name) {
  return request.arg != nullptr ? request.arg(name, request.context)
                                : std::string();
}

bool read_list_number(const DeviceBulkRequest &request, const char *name,
                       size_t minimum, size_t maximum, size_t &value) {
  if (!request_has_arg(request, name)) return true;
  const auto text = request_arg(request, name);
  size_t parsed = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc() || result.ptr != text.data() + text.size() ||
      parsed < minimum || parsed > maximum) return false;
  value = parsed;
  return true;
}

size_t json_string_bytes(const std::string &text) {
  size_t bytes = 2; // enclosing quotes
  for (const unsigned char ch : text)
    bytes += ch < 0x20 ? 6 : (ch == '"' || ch == '\\' ? 2 : 1);
  return bytes;
}

bool append_bulk_fs_item(const StorageListEntry &entry, void *context) {
  if (storage_private_path(entry.path)) return true;
  BulkFsListContext *list_context = static_cast<BulkFsListContext *>(context);
  if (list_context == nullptr || list_context->items == nullptr) {
    return false;
  }
  if (list_context->visited++ < list_context->offset) return true;
  if (list_context->count == list_context->limit) {
    list_context->more = true;
    return false;
  }
  if (entry.path.size() > kLittleFsListMaximumPathBytes) {
    list_context->error = "directory_entry_path_too_long";
    return false;
  }

  const std::string full_path = storage_normalize_path(entry.path.c_str());
  const std::string::size_type slash_index = full_path.find_last_of('/');
  const std::string display_name =
      slash_index != std::string::npos ? full_path.substr(slash_index + 1)
                                       : full_path;

  // Include worst-case numeric fields, punctuation and escaped text before
  // allocating the JSON item. The fixed envelope reserve also covers paging.
  const size_t item_bytes = json_string_bytes(full_path) +
                            json_string_bytes(display_name) + 128;
  if (item_bytes > kLittleFsListMaximumJsonBytes - list_context->encoded_bytes) {
    list_context->more = true;
    return false;
  }

  JsonObject item = list_context->items->add<JsonObject>();
  if (item.isNull() || !item["name"].set(display_name) ||
      !item["path"].set(full_path) || !item["size"].set(entry.size_bytes) ||
      !item["isDirectory"].set(entry.is_directory) ||
      !item["modifiedMs"].set(entry.modified_ms)) {
    list_context->error = "directory_response_allocation_failed";
    return false;
  }
  ++list_context->count;
  list_context->encoded_bytes += item_bytes;
  return true;
}

bool upload_file_stream(StorageFileChunkWriter writer, void *writer_context,
                        void *producer_context, std::string &error) {
  BulkFsUploadContext *upload_context =
      static_cast<BulkFsUploadContext *>(producer_context);
  if (upload_context == nullptr || upload_context->request == nullptr ||
      writer == nullptr) {
    error = "invalid_upload_context";
    return false;
  }

  const DeviceBulkRequest &request = *upload_context->request;
  if (request.body_data != nullptr && request.body_size > 0) {
    upload_context->remaining = 0;
    return writer(request.body_data, request.body_size, writer_context);
  }

  if (request.read_chunk == nullptr || request.content_length == 0) {
    error = "missing_upload_body";
    return false;
  }

  uint8_t buffer[kLittleFsUploadChunkBytes];
  size_t remaining = request.content_length;
  while (remaining > 0) {
    const size_t wanted = std::min(sizeof(buffer), remaining);
    const int read = request.read_chunk(buffer, wanted, request.context);
    if (read <= 0 || static_cast<size_t>(read) > wanted) {
      error = "upload_body_read_failed";
      return false;
    }
    const size_t read_size = static_cast<size_t>(read);
    if (!writer(buffer, read_size, writer_context)) {
      error = "upload_file_write_failed";
      return false;
    }
    remaining -= std::min(read_size, remaining);
  }
  upload_context->remaining = 0;
  return true;
}

bool append_text_file_chunk(const uint8_t *data, size_t size, void *context) {
  TextReadContext *read_context = static_cast<TextReadContext *>(context);
  if (read_context == nullptr || read_context->content == nullptr ||
      (size > 0 && data == nullptr)) {
    return false;
  }
  if (size == 0) {
    return true;
  }
  if (read_context->bytes_read + size > read_context->max_bytes) {
    read_context->overflow = true;
    return false;
  }

  read_context->content->append(reinterpret_cast<const char *>(data), size);
  read_context->bytes_read += size;
  return true;
}

bool send_media_file_chunk(const uint8_t *data, size_t size, void *context) {
  MediaFileWriterContext *media_context =
      static_cast<MediaFileWriterContext *>(context);
  return media_context != nullptr && media_context->writer != nullptr &&
         media_context->writer->send_chunk != nullptr &&
         media_context->writer->send_chunk(data, size,
                                           media_context->writer->context);
}

const char *content_type_for_path(const std::string &path) {
  std::string value = path;
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  if (value.size() > 4 && value.compare(value.size() - 4, 4, ".jpg") == 0) {
    return "image/jpeg";
  }
  if (value.size() > 5 && value.compare(value.size() - 5, 5, ".jpeg") == 0) {
    return "image/jpeg";
  }
  if (value.size() > 4 && value.compare(value.size() - 4, 4, ".png") == 0) {
    return "image/png";
  }
  if (value.size() > 4 && value.compare(value.size() - 4, 4, ".bmp") == 0) {
    return "image/bmp";
  }
  return "application/octet-stream";
}

} // namespace

DeviceOperationResult handle_littlefs_bulk_list_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  const std::string path = storage_normalize_path(
      request_has_arg(request, "path") ? request_arg(request, "path").c_str()
                                       : "/");
  if (path.size() > kLittleFsListMaximumPathBytes ||
      !storage_is_safe_path(path.c_str()) || (storage_pool_managed_path(path) || storage_private_path(path))) {
    return device_operation_error("invalid_path", 400,
                                     "filesystem_request_failed");
  }

  BulkFsListContext context;
  const bool paginated = request_has_arg(request, "offset") ||
                         request_has_arg(request, "limit");
  if (!read_list_number(request, "offset", 0, UINT32_MAX - kLittleFsListMaximumItems,
                         context.offset) ||
      !read_list_number(request, "limit", 1, kLittleFsListMaximumItems,
                         context.limit))
    return device_operation_error("invalid_directory_page", 400,
                                   "filesystem_request_failed");
  context.encoded_bytes = json_string_bytes(path) + 256;

  if (!storage_path_exists(path.c_str())) {
    return device_operation_error("directory_not_found", 404,
                                     "filesystem_request_failed");
  }

  if (!payload["ok"].set(true) || !payload["path"].set(path))
    return device_operation_error("directory_response_allocation_failed", 503,
                                   "filesystem_request_failed");
  JsonArray items = payload["items"].to<JsonArray>();
  if (items.isNull())
    return device_operation_error("directory_response_allocation_failed", 503,
                                   "filesystem_request_failed");
  context.items = &items;
  if (!storage_list_entries(path.c_str(), append_bulk_fs_item,
                                   &context)) {
    return device_operation_error("directory_not_found", 404,
                                     "filesystem_request_failed");
  }

  if (context.error != nullptr || (context.more && (!paginated || context.count == 0))) {
    payload.clear(); // Do not return a silently incomplete legacy directory.
    const char *error = context.error != nullptr ? context.error
                                                : "directory_requires_pagination";
    return device_operation_error(error,
        context.error != nullptr &&
            std::string_view(context.error) == "directory_response_allocation_failed"
            ? 503 : 413, "filesystem_request_failed");
  }
  if (paginated) {
    if (!payload["offset"].set(context.offset) ||
        !payload["limit"].set(context.limit) ||
        !payload["has_more"].set(context.more) ||
        (context.more && !payload["next_offset"].set(context.offset + context.count))) {
      payload.clear();
      return device_operation_error("directory_response_allocation_failed", 503,
                                     "filesystem_request_failed");
    }
  }

  return device_operation_ack();
}

DeviceOperationResult handle_littlefs_bulk_read_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  const std::string path =
      storage_normalize_path(request_arg(request, "path").c_str());
  if (!storage_is_safe_path(path.c_str()) || (storage_pool_managed_path(path) || storage_private_path(path))) {
    return device_operation_error("invalid_path", 400,
                                     "filesystem_request_failed");
  }

  std::string content;
  if (!storage_path_exists(path.c_str())) {
    return device_operation_error("file_not_found", 404,
                                     "filesystem_request_failed");
  }
  TextReadContext read_context;
  read_context.content = &content;
  read_context.max_bytes = kLittleFsMaxReadPayloadBytes;
  std::string error;
  if (!storage_stream_file(path.c_str(), append_text_file_chunk,
                                  &read_context, &error)) {
    if (read_context.overflow) {
      return device_operation_error("file_too_large", 413,
                                       "filesystem_request_failed");
    }
    return device_operation_error(
        error.empty() ? "read_failed" : error.c_str(), 500,
        "filesystem_request_failed");
  }

  payload["ok"] = true;
  payload["path"] = path;
  payload["content"] = content;
  return device_operation_ack();
}

DeviceOperationResult handle_littlefs_bulk_write_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  if (request.body_size > kLittleFsMaxWritePayloadBytes) {
    return device_operation_error("bulk_payload_too_large", 413,
                                     "filesystem_request_failed");
  }

  const std::string path =
      storage_normalize_path(
          device_operation_string(request.payload["path"]).c_str());
  const std::string content = device_operation_string(request.payload["content"]);
  if (!storage_is_safe_path(path.c_str()) || (storage_pool_managed_path(path) || storage_private_path(path))) {
    return device_operation_error("invalid_path", 400,
                                     "filesystem_request_failed");
  }

  std::string quota_error;
  if (!storage_pool_admit(content.size(), false, quota_error)) return device_operation_error(quota_error.c_str(), 507, "filesystem_request_failed");
  if (!storage_write_text_file(path.c_str(), content.c_str())) {
    return device_operation_error("write_failed", 500,
                                     "filesystem_request_failed");
  }

  payload["ok"] = true;
  payload["path"] = path;
  return device_operation_ack();
}

DeviceOperationResult handle_littlefs_bulk_upload_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  const std::string path =
      storage_normalize_path(request_arg(request, "path").c_str());
  if (!storage_is_safe_path(path.c_str()) || (storage_pool_managed_path(path) || storage_private_path(path)) || path == "/") {
    return device_operation_error("invalid_path", 400,
                                     "filesystem_request_failed");
  }

  std::string quota_error;
  if (!storage_pool_admit(request.content_length, false, quota_error)) return device_operation_error(quota_error.c_str(), 507, "filesystem_request_failed");
  BulkFsUploadContext upload_context;
  upload_context.request = &request;
  upload_context.remaining = request.content_length;
  size_t written_bytes = 0;
  std::string error;
  if (!storage_write_sized_streamed_file(
          path.c_str(), request.content_length, upload_file_stream, &upload_context, &written_bytes,
          &error)) {
    return device_operation_error(
        error.empty() ? "upload_failed" : error.c_str(), 500,
        "filesystem_request_failed");
  }

  payload["ok"] = true;
  payload["path"] = path;
  payload["size"] = static_cast<uint64_t>(written_bytes);
  payload["content_type"] = content_type_for_path(path);
  return device_operation_ack();
}

DeviceOperationResult handle_littlefs_bulk_delete_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  const std::string path =
      storage_normalize_path(
          device_operation_string(request.payload["path"]).c_str());
  if (!storage_is_safe_path(path.c_str()) || (storage_pool_managed_path(path) || storage_private_path(path))) {
    return device_operation_error("invalid_path", 400,
                                     "filesystem_request_failed");
  }

  if (!storage_ready()) {
    return device_operation_error("delete_failed", 500,
                                     "filesystem_request_failed");
  }
  if (!storage_path_exists(path.c_str())) {
    return device_operation_error("file_not_found", 404,
                                     "filesystem_request_failed");
  }
  if (!storage_delete_file(path.c_str())) {
    return device_operation_error("delete_failed", 500,
                                     "filesystem_request_failed");
  }

  payload["ok"] = true;
  payload["path"] = path;
  return device_operation_ack();
}

DeviceOperationResult handle_littlefs_bulk_mkdir_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  const std::string path =
      storage_normalize_path(
          device_operation_string(request.payload["path"]).c_str());
  if (!storage_is_safe_path(path.c_str()) || (storage_pool_managed_path(path) || storage_private_path(path)) || path == "/") {
    return device_operation_error("invalid_path", 400,
                                     "filesystem_request_failed");
  }

  if (!storage_ensure_directory(path.c_str())) {
    return device_operation_error("mkdir_failed", 500,
                                     "filesystem_request_failed");
  }

  payload["ok"] = true;
  payload["path"] = path;
  return device_operation_ack();
}

DeviceOperationResult handle_littlefs_bulk_format_request([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject payload) {
  if (storage_pool_enabled()) return device_operation_error("shared_pool_format_forbidden", 403, "filesystem_request_failed");
  const std::string confirmation =
      device_operation_string(request.payload["confirm"]);
  if (confirmation != "FORMAT_LITTLEFS") {
    return device_operation_error("format_confirmation_required", 400,
                                     "filesystem_request_failed");
  }

  if (!storage_format(device.model.capabilities)) {
    return device_operation_error(storage_last_error(), 500,
                                     "filesystem_request_failed");
  }

  payload["ok"] = true;
  payload["formatted"] = true;
  payload["mount_path"] = storage_mount_path();
  payload["total_bytes"] = storage_total_bytes();
  payload["used_bytes"] = storage_used_bytes();
  return device_operation_ack();
}

DeviceOperationResult handle_littlefs_media_file_request([[maybe_unused]] DeviceApiContext &device, const DeviceMediaRequest &request, const DeviceMediaWriter &writer) {
  DeviceOperationResult result;
  if (writer.begin_stream == nullptr || writer.send_chunk == nullptr ||
      writer.end_stream == nullptr) {
    result.ok = false;
    result.status_code = 500;
    result.error = "missing_media_writer";
    return result;
  }

  const std::string path = storage_normalize_path(request.path.c_str());
  if (!storage_is_safe_path(path.c_str()) || (storage_pool_managed_path(path) || storage_private_path(path)) || path == "/") {
    result.ok = false;
    result.status_code = 400;
    result.error = "invalid_path";
    return result;
  }
  if (!storage_path_exists(path.c_str())) {
    result.ok = false;
    result.status_code = 404;
    result.error = "file_not_found";
    return result;
  }

  if (!writer.begin_stream(200, content_type_for_path(path), writer.context)) {
    result.ok = false;
    result.status_code = 503;
    result.error = "file_stream_start_failed";
    return result;
  }

  MediaFileWriterContext media_context;
  media_context.writer = &writer;
  std::string stream_error;
  const bool streamed = storage_stream_file(
      path.c_str(), send_media_file_chunk, &media_context, &stream_error);
  writer.end_stream(writer.context);
  if (!streamed) {
    result.ok = false;
    result.status_code = 503;
    result.error =
        stream_error.empty() ? "file_stream_failed" : stream_error;
  }
  return result;
}

} // namespace lunaredu::lunaros
