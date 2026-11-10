#include "lunaredu_lunaros/delivery/update.h"

#include "lunaredu_lunaros/network/http_client.h"
#include <psa/crypto.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "delivery/update/ota_status_internal.h"
#include "network/http/network_transport_policy.h"

namespace lunaredu::lunaros {

namespace {

constexpr const char *kTag = "system.ota.pull";

struct HttpFirmwareStream {
  std::unique_ptr<lunaredu::lunaros::NetworkDownloadReader> reader;
  psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
  std::array<uint8_t, 32> expected{};
  bool sha_started = false;
  bool sha_failed = false;
  size_t expected_size = 0;
  size_t bytes_read = 0;
  char read_error[96] = {};
};

struct OtaPullJob {
  lunaredu::lunaros::NetworkJobHandle handle;
  std::string url;
  std::string expected_sha256;
  size_t expected_size = 0;
};

std::mutex g_pull_mutex;
OtaPullJob g_pull_job;

bool decode_sha256(const std::string &value, std::array<uint8_t, 32> &output) {
  if (value.size() != 64) return false;
  for (size_t index = 0; index < output.size(); ++index) {
    const char high = static_cast<char>(std::tolower(value[index * 2]));
    const char low = static_cast<char>(std::tolower(value[index * 2 + 1]));
    const auto nibble = [](char value) -> int {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      return -1;
    };
    const int left = nibble(high);
    const int right = nibble(low);
    if (left < 0 || right < 0) return false;
    output[index] = static_cast<uint8_t>((left << 4) | right);
  }
  return true;
}

int read_http_chunk(uint8_t *buffer, size_t max_size, void *context) {
  auto *stream = static_cast<HttpFirmwareStream *>(context);
  if (stream == nullptr || stream->reader == nullptr) return -1;
  const int read = stream->reader->Read(reinterpret_cast<char *>(buffer), max_size);
  if (read > 0) {
    stream->bytes_read += static_cast<size_t>(read);
    if (psa_hash_update(&stream->sha, buffer, static_cast<size_t>(read)) !=
        PSA_SUCCESS) {
      stream->sha_failed = true;
      std::snprintf(stream->read_error, sizeof(stream->read_error),
                    "ota_hash_update_failed:%u/%u",
                    static_cast<unsigned>(stream->bytes_read),
                    static_cast<unsigned>(stream->expected_size));
      return -1;
    }
  } else {
    std::snprintf(stream->read_error, sizeof(stream->read_error),
                  "ota_read_failed:%u/%u:error=%d:%s",
                  static_cast<unsigned>(stream->bytes_read),
                  static_cast<unsigned>(stream->expected_size),
                  stream->reader->GetLastError(), stream->reader->error_detail().c_str());
    LOGW(kTag, "%s", stream->read_error);
  }
  return read;
}

const char *http_stream_read_error(void *context) {
  auto *stream = static_cast<HttpFirmwareStream *>(context);
  return stream == nullptr ? nullptr : stream->read_error;
}

bool validate_http_stream(void *context) {
  auto *stream = static_cast<HttpFirmwareStream *>(context);
  if (stream == nullptr || !stream->sha_started || stream->sha_failed) {
    return false;
  }
  std::array<uint8_t, 32> actual{};
  size_t actual_size = 0;
  if (psa_hash_finish(&stream->sha, actual.data(), actual.size(),
                      &actual_size) != PSA_SUCCESS ||
      actual_size != actual.size()) {
    return false;
  }
  stream->sha_started = false;
  return std::memcmp(actual.data(), stream->expected.data(), actual.size()) == 0;
}

void close_http_stream(HttpFirmwareStream &stream) {
  if (stream.sha_started) {
    psa_hash_abort(&stream.sha);
    stream.sha_started = false;
  }
  if (stream.reader) {
    stream.reader->Close();
    stream.reader.reset();
  }
}

bool allowed_ota_transport(const std::string &url) {
  return network_transport_security_allows(
      url, NetworkTransportSecurity::AllowPlaintextDevelopment, true);
}

bool install_from_url(const std::string &url,
                      const std::string &expected_sha256,
                      size_t expected_size) {
  const size_t partition_capacity = ota_service_image_metadata_snapshot().next.size;
  if (!allowed_ota_transport(url) || expected_size == 0 ||
      partition_capacity == 0 || expected_size > partition_capacity) {
    ota_service_report_failure("invalid_ota_pull_request");
    return false;
  }

  HttpFirmwareStream source;
  source.expected_size = expected_size;
  if (!decode_sha256(expected_sha256, source.expected)) {
    ota_service_report_failure("invalid_ota_pull_sha256");
    return false;
  }
  if (psa_hash_setup(&source.sha, PSA_ALG_SHA_256) != PSA_SUCCESS) {
    ota_service_report_failure("ota_sha256_init_failed");
    close_http_stream(source);
    return false;
  }
  source.sha_started = true;

  NetworkDownloadRequest request;
  request.url = url;
  request.timeout_ms = 120000;
  request.keep_alive = true;
  request.transport_security = NetworkTransportSecurity::AllowPlaintextDevelopment;
  NetworkDownloadFailure failure;
  source.reader = network_start_download(std::move(request), &failure);
  if (!source.reader) {
    char message[160];
    const char *code = failure.status_code < 0 ? "ota_http_open_failed" :
        failure.status_code != 200 ? "ota_http_status_failed" : "ota_http_size_mismatch";
    std::snprintf(message, sizeof(message), "%s:%d:error=%d:%s",
                  code, failure.status_code, failure.error, failure.detail.c_str());
    ota_service_report_failure(message);
    close_http_stream(source);
    return false;
  }
  if (source.reader->content_length() != expected_size) {
    char message[96];
    std::snprintf(message, sizeof(message), "ota_http_size_mismatch:%u/%u",
                  static_cast<unsigned>(source.reader->content_length()),
                  static_cast<unsigned>(expected_size));
    ota_service_report_failure(message);
    close_http_stream(source);
    return false;
  }

  OtaFirmwareStream stream;
  stream.content_length = expected_size;
  stream.context = &source;
  stream.read_chunk = read_http_chunk;
  stream.read_error = http_stream_read_error;
  stream.validate = validate_http_stream;
  const bool installed = ota_service_install_stream(stream);
  close_http_stream(source);
  return installed;
}

bool retryable_pull_failure(const std::string &message) {
  const auto matches = [&message](const char *code) {
    const size_t length = std::strlen(code);
    return message.compare(0, length, code) == 0 &&
           (message.size() == length || message[length] == ':');
  };
  return matches("ota_http_open_failed") ||
         matches("ota_http_status_failed") || matches("ota_read_failed");
}

bool retry_attempt_available(lunaredu::lunaros::NetworkJobHandle handle) {
  const auto jobs = lunaredu::lunaros::network_job_manager_snapshot();
  for (size_t index = 0; index < jobs.record_count; ++index) {
    const auto &job = jobs.jobs[index];
    if (job.handle.id == handle.id) {
      return job.attempts < job.maximum_attempts;
    }
  }
  return false;
}

lunaredu::lunaros::NetworkJobRunResult run_ota_pull(
    lunaredu::lunaros::NetworkJobHandle handle, void *) {
  std::string url;
  std::string sha256;
  size_t size = 0;
  {
    std::lock_guard<std::mutex> lock(g_pull_mutex);
    if (g_pull_job.handle.id != handle.id) {
      return {false, false, "ota_pull_context_stale"};
    }
    url = g_pull_job.url;
    sha256 = g_pull_job.expected_sha256;
    size = g_pull_job.expected_size;
  }

  ota_service_report_pull_started(handle.id);
  const bool installed = install_from_url(url, sha256, size);
  const OtaServiceStatus status = ota_service_status_snapshot();
  const bool retryable = !installed && retryable_pull_failure(status.message) &&
                         retry_attempt_available(handle);
  ota_service_report_pull_finished(handle.id);
  if (retryable) {
    ota_service_report_pull_queued(handle.id);
  } else {
    std::lock_guard<std::mutex> lock(g_pull_mutex);
    if (g_pull_job.handle.id == handle.id) {
      g_pull_job = {};
    }
  }
  return {installed, retryable,
          installed ? "ota_pull_complete" : status.message};
}

} // namespace

bool ota_service_queue_install_from_https(const std::string &url,
                                          const std::string &expected_sha256,
                                          size_t expected_size,
                                          uint32_t &job_id,
                                          std::string &error) {
  job_id = 0;
  error.clear();
  if (network_maintenance_state() !=
      NetworkMaintenanceState::OtaExclusive) {
    error = "ota_network_maintenance_not_exclusive";
    return false;
  }
  const size_t partition_capacity = ota_service_image_metadata_snapshot().next.size;
  if (!allowed_ota_transport(url) || expected_size == 0 ||
      partition_capacity == 0 || expected_size > partition_capacity) {
    error = "invalid_ota_pull_request";
    return false;
  }
  std::array<uint8_t, 32> decoded_sha{};
  if (!decode_sha256(expected_sha256, decoded_sha)) {
    error = "invalid_ota_pull_sha256";
    return false;
  }

  std::lock_guard<std::mutex> lock(g_pull_mutex);
  if (g_pull_job.handle) {
    error = "ota_pull_in_progress";
    return false;
  }
  if (ota_service_upload_active() || ota_service_restart_pending()) {
    error = ota_service_upload_active() ? "ota_upload_in_progress"
                                        : "ota_restart_pending";
    return false;
  }

  g_pull_job.url = url;
  g_pull_job.expected_sha256 = expected_sha256;
  g_pull_job.expected_size = expected_size;
  lunaredu::lunaros::NetworkJobHandle handle;
  NetworkJobDefinition definition{
      "system.update", "firmware_pull", kNetworkPriorityCritical, 3, 0,
      run_ota_pull, nullptr, false, 5000, 5000};
  definition.job_class = NetworkJobClass::Ota;
  if (!submit_network_job(definition, handle, error)) {
    g_pull_job = {};
    return false;
  }
  g_pull_job.handle = handle;
  job_id = handle.id;
  ota_service_report_pull_queued(job_id);
  return true;
}

} // namespace lunaredu::lunaros
