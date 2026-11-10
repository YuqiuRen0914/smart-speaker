#include "network/http/http_request_internal.h"

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <mbedtls/error.h>
#include <mbedtls/x509_crt.h>

#include <cstdio>
#include <utility>

namespace lunaredu::lunaros {

namespace {

esp_http_client_method_t to_http_method(const std::string &method) {
  if (method == "POST") {
    return HTTP_METHOD_POST;
  }
  if (method == "PUT") {
    return HTTP_METHOD_PUT;
  }
  if (method == "PATCH") {
    return HTTP_METHOD_PATCH;
  }
  if (method == "DELETE") {
    return HTTP_METHOD_DELETE;
  }
  if (method == "HEAD") {
    return HTTP_METHOD_HEAD;
  }
  if (method == "OPTIONS") {
    return HTTP_METHOD_OPTIONS;
  }
  return HTTP_METHOD_GET;
}

std::string format_esp_error(int error) {
  if (error == 0) {
    return "0/ESP_OK";
  }
  char name[32] = {};
  esp_err_to_name_r(error, name, sizeof(name));
  char buffer[96] = {};
  std::snprintf(buffer, sizeof(buffer), "%d/%s", error, name);
  return buffer;
}

std::string format_mbed_tls_error(int error) {
  if (error == 0) {
    return {};
  }
  char text[128] = {};
  mbedtls_strerror(error, text, sizeof(text));
  char buffer[192] = {};
  std::snprintf(buffer, sizeof(buffer), "%d/%s", error, text);
  return buffer;
}

std::string format_verify_flags(int flags) {
  if (flags == 0) {
    return {};
  }
  char text[256] = {};
  if (mbedtls_x509_crt_verify_info(text, sizeof(text), " ",
                                   static_cast<uint32_t>(flags)) <= 0) {
    return std::to_string(flags);
  }
  return std::to_string(flags) + "/" + text;
}

class EspHttpRequestStream final : public NetworkRequestStream {
public:
  explicit EspHttpRequestStream(NetworkRequest request)
      : request_(std::move(request)) {}
  ~EspHttpRequestStream() override { Close(); }

  static std::unique_ptr<NetworkRequestStream>
  create(NetworkRequest request, int *failure_error,
         std::string *failure_error_detail) {
    auto stream = std::unique_ptr<EspHttpRequestStream>(
        new EspHttpRequestStream(std::move(request)));
    if (!stream->begin()) {
      if (failure_error != nullptr) {
        *failure_error = stream->GetLastError();
      }
      if (failure_error_detail != nullptr) {
        *failure_error_detail = stream->GetLastErrorDetail();
      }
      return nullptr;
    }
    if (failure_error != nullptr) {
      *failure_error = 0;
    }
    if (failure_error_detail != nullptr) {
      failure_error_detail->clear();
    }
    return stream;
  }

  int Read(char *buffer, size_t buffer_size) override {
    if (!ensure_headers_fetched()) {
      return -1;
    }
    if (client_ == nullptr || response_complete_) {
      return 0;
    }
    const int read_size =
        esp_http_client_read(client_, buffer, static_cast<int>(buffer_size));
    if (read_size < 0) {
      last_error_ = read_size;
      capture_http_client_error("read");
      return -1;
    }
    if (read_size == 0) {
      if (!esp_http_client_is_complete_data_received(client_)) {
        last_error_ = ESP_ERR_INVALID_SIZE;
        last_error_detail_ = "response_body_incomplete";
        return -1;
      }
      response_complete_ = true;
    }
    return read_size;
  }

  int GetStatusCode() override {
    return ensure_headers_fetched() ? status_code_ : -1;
  }
  size_t GetBodyLength() override {
    if (!ensure_headers_fetched() || content_length_ < 0) {
      return 0;
    }
    return static_cast<size_t>(content_length_);
  }
  int GetLastError() override { return last_error_; }
  std::string GetLastErrorDetail() override { return last_error_detail_; }
  void Close() override {
    if (client_ == nullptr) {
      return;
    }
    if (opened_) {
      esp_http_client_close(client_);
      opened_ = false;
    }
    esp_http_client_cleanup(client_);
    client_ = nullptr;
  }

private:
  bool begin() {
    esp_http_client_config_t config = {};
    config.url = request_.url.c_str();
    config.timeout_ms = request_.timeout_ms;
    config.keep_alive_enable = request_.keep_alive;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 2048;
    config.buffer_size_tx = 2048;
    client_ = esp_http_client_init(&config);
    if (client_ == nullptr) {
      last_error_ = ESP_FAIL;
      return false;
    }

    esp_http_client_set_method(client_, to_http_method(request_.method));
    for (const auto &header : request_.headers) {
      esp_http_client_set_header(client_, header.key.c_str(),
                                 header.value.c_str());
    }

    const int write_len =
        static_cast<int>(request_.body ? request_.body->size() : 0);
    const esp_err_t open_error = esp_http_client_open(client_, write_len);
    if (open_error != ESP_OK) {
      last_error_ = open_error;
      capture_http_client_error("open");
      Close();
      return false;
    }
    opened_ = true;

    if (request_.body.has_value() && !request_.body->empty() &&
        !write_raw_all(request_.body->data(), request_.body->size())) {
      capture_http_client_error("write_body");
      Close();
      return false;
    }
    request_complete_ = true;
    return ensure_headers_fetched();
  }

  bool ensure_headers_fetched() {
    if (headers_fetched_) {
      return true;
    }
    if (client_ == nullptr || !request_complete_) {
      last_error_ = ESP_FAIL;
      return false;
    }
    const int64_t fetch_result = esp_http_client_fetch_headers(client_);
    if (fetch_result < 0) {
      last_error_ = static_cast<int>(fetch_result);
      capture_http_client_error("fetch_headers");
      return false;
    }
    status_code_ = esp_http_client_get_status_code(client_);
    content_length_ = esp_http_client_get_content_length(client_);
    headers_fetched_ = true;
    return true;
  }

  bool write_raw_all(const char *buffer, size_t buffer_size) {
    size_t total_written = 0;
    while (total_written < buffer_size) {
      const int written = esp_http_client_write(
          client_, buffer + total_written,
          static_cast<int>(buffer_size - total_written));
      if (written <= 0) {
        last_error_ = written;
        capture_http_client_error("write");
        return false;
      }
      total_written += static_cast<size_t>(written);
    }
    return true;
  }

  void capture_http_client_error(const char *stage) {
    if (client_ == nullptr) {
      return;
    }
    int tls_code = 0;
    int tls_flags = 0;
    const esp_err_t tls_error = esp_http_client_get_and_clear_last_tls_error(
        client_, &tls_code, &tls_flags);
    const int socket_error = esp_http_client_get_errno(client_);
    const esp_http_client_transport_t transport =
        esp_http_client_get_transport_type(client_);

    std::string detail = "stage=";
    detail += stage == nullptr ? "unknown" : stage;
    detail += " transport=" + std::to_string(static_cast<int>(transport));
    detail += " err=" + format_esp_error(last_error_);
    detail += " tls_result=" + format_esp_error(tls_error);
    detail += " tls_code=" + format_mbed_tls_error(tls_code);
    detail += " tls_flags=" + format_verify_flags(tls_flags);
    detail += " errno=" + std::to_string(socket_error);
    last_error_detail_ = std::move(detail);
  }

  NetworkRequest request_;
  esp_http_client_handle_t client_ = nullptr;
  bool opened_ = false;
  bool request_complete_ = false;
  bool headers_fetched_ = false;
  bool response_complete_ = false;
  int status_code_ = -1;
  int64_t content_length_ = 0;
  int last_error_ = 0;
  std::string last_error_detail_;
};

} // namespace

std::unique_ptr<NetworkRequestStream>
create_esp_http_request_stream(NetworkRequest request, int *failure_error,
                               std::string *failure_error_detail) {
  return EspHttpRequestStream::create(std::move(request), failure_error,
                                      failure_error_detail);
}

} // namespace lunaredu::lunaros
