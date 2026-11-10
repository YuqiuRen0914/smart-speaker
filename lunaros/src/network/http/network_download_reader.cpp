#include "network/http/http_request_internal.h"

#include <esp_timer.h>

#include <utility>

namespace lunaredu::lunaros {

namespace {

class NetworkDownloadReaderAdapter final : public NetworkDownloadReader {
public:
  NetworkDownloadReaderAdapter(std::unique_ptr<NetworkRequestStream> stream,
                               size_t content_length,
                               NetworkDownloadProgressCallback callback)
      : stream_(std::move(stream)), content_length_(content_length),
        progress_callback_(std::move(callback)) {}

  int Read(char *buffer, size_t buffer_size) override {
    if (!stream_) {
      return -1;
    }
    const int read_size = stream_->Read(buffer, buffer_size);
    if (read_size <= 0) {
      return read_size;
    }

    total_read_ += static_cast<size_t>(read_size);
    recent_read_ += static_cast<size_t>(read_size);
    const int64_t now = esp_timer_get_time();
    if (last_calc_time_ == 0) {
      last_calc_time_ = now;
    }
    if (progress_callback_ != nullptr &&
        (now - last_calc_time_ >= 1000000 || total_read_ >= content_length_)) {
      NetworkDownloadProgress progress;
      progress.total_read = total_read_;
      progress.content_length = content_length_;
      progress.progress =
          content_length_ == 0
              ? 0
              : static_cast<int>((total_read_ * 100) / content_length_);
      progress.speed = recent_read_;
      progress_callback_(progress);
      last_calc_time_ = now;
      recent_read_ = 0;
    }
    return read_size;
  }

  size_t content_length() const override { return content_length_; }
  size_t total_read() const override { return total_read_; }
  int GetLastError() const override {
    return stream_ ? stream_->GetLastError() : -1;
  }
  std::string error_detail() const override {
    return stream_ ? stream_->GetLastErrorDetail() : "download_closed";
  }
  void Close() override {
    if (stream_) {
      stream_->Close();
    }
  }

private:
  std::unique_ptr<NetworkRequestStream> stream_;
  size_t content_length_ = 0;
  size_t total_read_ = 0;
  size_t recent_read_ = 0;
  int64_t last_calc_time_ = 0;
  NetworkDownloadProgressCallback progress_callback_;
};

} // namespace

std::unique_ptr<NetworkDownloadReader> create_network_download_reader(
    std::unique_ptr<NetworkRequestStream> stream, size_t content_length,
    NetworkDownloadProgressCallback callback) {
  return std::make_unique<NetworkDownloadReaderAdapter>(
      std::move(stream), content_length, std::move(callback));
}

} // namespace lunaredu::lunaros
