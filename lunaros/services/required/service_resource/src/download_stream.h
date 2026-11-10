#pragma once

#include <array>
#include "lunaros_digest.h"
#include "lunaredu_download.h"
#include "lunaros_storage.h"

namespace lunaredu::lunaros::internal {
// Shared bounded streaming primitive. Publication, account scope and persisted
// records stay with their owners; this object lives only for one synchronous job.
struct DownloadStream {
  DownloadReader *reader = nullptr;
  size_t maximum_bytes = 0;
  size_t total = 0;
  digest::Sha256 *hash = nullptr;
  bool (*cancelled)(const void *) = nullptr;
  const void *cancellation_context = nullptr;
  bool exact_length = false;
  void (*on_progress)(size_t, const void *) = nullptr;
  const void *progress_context = nullptr;
};
inline bool copy_download_stream(StorageFileChunkWriter writer, void *target,
                                 void *opaque, std::string &error) {
  auto &stream = *static_cast<DownloadStream *>(opaque);
  std::array<char, 1024> bytes{};
  for (;;) {
    if (stream.cancelled && stream.cancelled(stream.cancellation_context)) {
      error = "download_cancelled"; return false;
    }
    const int count = stream.reader->Read(bytes.data(), bytes.size());
    if (count < 0) {
      const std::string detail = stream.reader->error_detail();
      error = detail == "download_size_limit_exceeded"
          ? "resource_too_large"
          : detail == "download_length_mismatch"
                ? detail : "network_read_failed";
      return false;
    }
    if (!count) {
      if (stream.exact_length && stream.total != stream.maximum_bytes) {
        error = "download_length_mismatch"; return false;
      }
      return true;
    }
    if (stream.cancelled && stream.cancelled(stream.cancellation_context)) {
      error = "download_cancelled"; return false;
    }
    const size_t size = static_cast<size_t>(count);
    if (size > bytes.size() || size > stream.maximum_bytes - stream.total) {
      error = "resource_too_large"; return false;
    }
    if (stream.hash && !stream.hash->update(reinterpret_cast<const uint8_t *>(bytes.data()), size)) {
      error = "sha256_update_failed"; return false;
    }
    if (!writer(reinterpret_cast<const uint8_t *>(bytes.data()), size, target)) {
      error = "storage_write_failed"; return false;
    }
    stream.total += size;
    if (stream.on_progress) stream.on_progress(stream.total, stream.progress_context);
  }
}
} // namespace lunaredu::lunaros::internal
