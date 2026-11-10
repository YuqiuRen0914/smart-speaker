#include "lunaros_network_request_stream.h"
#include "lunaredu_download.h"

#include <utility>

namespace lunaredu::lunaros {

namespace {

class NetworkDownloadReaderAdapter final : public DownloadReader {
public:
  NetworkDownloadReaderAdapter(std::unique_ptr<NetworkRequestStream> stream,
                               size_t content_length)
      : stream_(std::move(stream)), content_length_(content_length) {}

  int Read(char *buffer, size_t buffer_size) override {
    if (!stream_) {
      return -1;
    }
    const int read_size = stream_->Read(buffer, buffer_size);
    if (read_size <= 0) {
      return read_size;
    }

    total_read_ += static_cast<size_t>(read_size);
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
};

} // namespace

std::unique_ptr<DownloadReader> create_network_download_reader(
    std::unique_ptr<NetworkRequestStream> stream, size_t content_length) {
  return std::make_unique<NetworkDownloadReaderAdapter>(
      std::move(stream), content_length);
}

} // namespace lunaredu::lunaros
