#include "lunaredu_download.h"

#include <atomic>
#include <chrono>
#include <utility>

namespace lunaredu::lunaros {
namespace {
std::atomic<DownloadTransport> g_transport{nullptr};

class BoundedDownloadReader final : public DownloadReader {
 public:
  BoundedDownloadReader(std::unique_ptr<DownloadReader> source,
                        size_t length, size_t maximum,
                        DownloadProgressCallback progress)
      : source_(std::move(source)), length_(length), maximum_(maximum),
        progress_(std::move(progress)) {}

  int Read(char *buffer, size_t buffer_size) override {
    if (!source_ || failed_) return -1;
    if (buffer == nullptr || buffer_size == 0) return 0;
    const int count = source_->Read(buffer, buffer_size);
    if (count == 0 && total_ != length_) {
      failed_ = true;
      error_ = "download_length_mismatch";
      source_->Close();
      source_.reset();
      return -1;
    }
    if (count <= 0) return count;
    const size_t received = static_cast<size_t>(count);
    if (received > buffer_size || received > maximum_ - total_ ||
        received > length_ - total_) {
      failed_ = true;
      error_ = "download_size_limit_exceeded";
      source_->Close();
      source_.reset();
      return -1;
    }
    total_ += received;
    recent_ += received;
    const auto now = std::chrono::steady_clock::now();
    if (last_progress_ == std::chrono::steady_clock::time_point{})
      last_progress_ = now;
    if (progress_ &&
        (now - last_progress_ >= std::chrono::seconds(1) || total_ >= length_)) {
      progress_({total_, length_, static_cast<int>(total_ * 100 / length_),
                 recent_});
      recent_ = 0;
      last_progress_ = now;
    }
    return count;
  }

  size_t content_length() const override { return length_; }
  size_t total_read() const override { return total_; }
  int GetLastError() const override {
    return failed_ ? -1 : source_ ? source_->GetLastError() : -1;
  }
  std::string error_detail() const override {
    return failed_ ? error_ : source_ ? source_->error_detail() : "download_closed";
  }
  void Close() override {
    if (source_) source_->Close();
    source_.reset();
  }

 private:
  std::unique_ptr<DownloadReader> source_;
  size_t length_ = 0;
  size_t maximum_ = 0;
  size_t total_ = 0;
  size_t recent_ = 0;
  std::chrono::steady_clock::time_point last_progress_{};
  DownloadProgressCallback progress_;
  bool failed_ = false;
  std::string error_;
};
}

bool download_service_start(DownloadTransport transport) {
  if (transport == nullptr) return false;
  g_transport.store(transport, std::memory_order_release);
  return true;
}

void download_service_stop() {
  g_transport.store(nullptr, std::memory_order_release);
}

bool download_service_ready() {
  return g_transport.load(std::memory_order_acquire) != nullptr;
}

std::unique_ptr<DownloadReader> download_start(
    DownloadRequest request, DownloadFailure *failure) {
  if (failure != nullptr) *failure = {};
  const DownloadTransport transport =
      g_transport.load(std::memory_order_acquire);
  if (transport == nullptr || request.maximum_bytes == 0) {
    if (failure != nullptr) failure->detail = transport == nullptr
        ? "download_service_unavailable" : "download_size_limit_required";
    return nullptr;
  }
  const size_t maximum = request.maximum_bytes;
  DownloadProgressCallback progress = std::move(request.progress_callback);
  request.progress_callback = {};
  DownloadTransportResult opened = transport(std::move(request));
  if (failure != nullptr) {
    failure->status_code = opened.status_code;
    failure->error = opened.error;
    failure->detail = opened.detail;
  }
  if (!opened.reader) return nullptr;
  if (opened.status_code != 200 || opened.content_length == 0 ||
      opened.content_length > maximum) {
    if (failure != nullptr) {
      failure->detail = opened.status_code != 200
          ? "download_http_status_failed"
          : opened.content_length == 0
                ? "download_content_length_unavailable"
                : "download_size_limit_exceeded";
    }
    opened.reader->Close();
    return nullptr;
  }
  return std::make_unique<BoundedDownloadReader>(
      std::move(opened.reader), opened.content_length, maximum,
      std::move(progress));
}

}  // namespace lunaredu::lunaros

