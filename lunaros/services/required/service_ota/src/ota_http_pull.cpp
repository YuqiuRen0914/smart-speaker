#include "lunaredu_ota.h"

#include "lunaredu_download.h"
#include "lunaros_network_http_client.h"
#include "lunaros_digest.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <esp_log.h>
#include "lunaros_network_jobs.h"
#include "lunaros_ota_lifecycle.h"
#include "lunaros_network_transport_policy.h"
#include "ota_install_intent.h"
#include "lunaredu_ota_recovery.h"
#include <sdkconfig.h>
#include "platform/system.h"

namespace lunaredu::lunaros {
namespace hashing = ::lunaredu::lunaros::digest;

namespace {

constexpr const char *kTag = "system.ota.pull";

struct HttpFirmwareStream {
  std::unique_ptr<lunaredu::lunaros::DownloadReader> reader;
  hashing::Sha256 sha;
  std::array<uint8_t, 32> expected{};
  bool sha_started = false;
  bool sha_failed = false;
  size_t expected_size = 0;
  size_t bytes_read = 0;
  char read_error[96] = {};
  NetworkJobHandle handle;
};

struct OtaPullJob {
  lunaredu::lunaros::NetworkJobHandle handle;
  std::string url;
  std::string expected_sha256;
  size_t expected_size = 0;
  bool role_aware = false;
  OtaCloudInstallRequest role_request;
};

std::mutex g_pull_mutex;
OtaPullJob g_pull_job;

bool decode_sha256(const std::string &value, std::array<uint8_t,32> &output) {
  return hashing::decode_hex(value, output.data(), output.size(), hashing::HexCase::Any);
}

int read_http_chunk(uint8_t *buffer, size_t max_size, void *context) {
  auto *stream = static_cast<HttpFirmwareStream *>(context);
  if (stream == nullptr || stream->reader == nullptr) return -1;
  if (stream->handle && network_job_cancellation_requested(stream->handle)) {
    std::snprintf(stream->read_error, sizeof(stream->read_error), "ota_pull_cancelled");
    return -1;
  }
  const int read = stream->reader->Read(reinterpret_cast<char *>(buffer), max_size);
  if (read > 0) {
    stream->bytes_read += static_cast<size_t>(read);
    if (!stream->sha.update(buffer, static_cast<size_t>(read))) {
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
    ESP_LOGW(kTag, "%s", stream->read_error);
  }
  return read;
}

const char *http_stream_read_error(void *context) {
  auto *stream = static_cast<HttpFirmwareStream *>(context);
  return stream == nullptr ? nullptr : stream->read_error;
}

bool validate_http_stream(void *context) {
  auto *stream = static_cast<HttpFirmwareStream *>(context);
  if (stream == nullptr || !stream->sha_started || stream->sha_failed ||
      (stream->handle && network_job_cancellation_requested(stream->handle))) {
    return false;
  }
  std::array<uint8_t, 32> actual{};
  if (!stream->sha.finish(actual)) {
    return false;
  }
  stream->sha_started = false;
  return std::memcmp(actual.data(), stream->expected.data(), actual.size()) == 0;
}

void close_http_stream(HttpFirmwareStream &stream) {
  if (stream.sha_started) {
    (void)stream.sha.abort();
    stream.sha_started = false;
  }
  if (stream.reader) {
    stream.reader->Close();
    stream.reader.reset();
  }
}

bool allowed_ota_transport(const std::string &url) {
  return network_transport_security_allows(
      url, NetworkTransportSecurity::RequireTls);
}

bool install_from_url(const std::string &url,
                      const std::string &expected_sha256,
                      size_t expected_size, uint32_t job_id = 0,
                      const OtaCloudInstallRequest *role_request = nullptr) {
  const size_t partition_capacity = role_request && role_request->target_role=="boot" &&
      ota_cloud_recovery_layout(role_request->layout_id) ? 0x200000 : ota_writer_metadata().next.size;
  if (!allowed_ota_transport(url) || expected_size == 0 ||
      partition_capacity == 0 || expected_size > partition_capacity) {
    ota_service_report_failure("invalid_ota_pull_request");
    return false;
  }

  HttpFirmwareStream source;
  source.expected_size = expected_size;
  source.handle.id = job_id;
  if (!decode_sha256(expected_sha256, source.expected)) {
    ota_service_report_failure("invalid_ota_pull_sha256");
    return false;
  }
  if (!source.sha.begin()) {
    ota_service_report_failure("ota_sha256_init_failed");
    close_http_stream(source);
    return false;
  }
  source.sha_started = true;

  DownloadRequest request;
  request.url = url;
  request.maximum_bytes = expected_size;
  request.timeout_ms = 120000;
  request.keep_alive = true;
  request.transport_security = DownloadTransportSecurity::RequireTls;
  DownloadFailure failure;
  source.reader = download_start(std::move(request), &failure);
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
  stream.expected_sha256 = expected_sha256.c_str();
  stream.role_request = role_request;
  stream.bootstrap_update = role_request && role_request->target_role=="boot";
  const bool installed = job_id == 0 ? ota_service_install_stream(stream)
                                     : ota_service_install_pull_stream(stream, job_id);
  close_http_stream(source);
  return installed;
}

bool retryable_pull_failure(const std::string &message) {
  const auto matches = [&message](const char *code) {
    const size_t length = std::strlen(code);
    return message.compare(0, length, code) == 0 &&
           (message.size() == length || message[length] == ':');
  };
  if (matches("ota_http_status_failed")) {
    // Authentication/expired URLs and permanent 4xx require a new authorized
    // request, not repeated writes with the same ticket.
    const auto status_start = message.find(':');
    if (status_start == std::string::npos) return false;
    const int status = std::atoi(message.c_str() + status_start + 1);
    return status == 408 || status == 429 || (status >= 500 && status <= 599);
  }
  return matches("ota_http_open_failed") || matches("ota_read_failed");
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
  bool role_aware = false;
  OtaCloudInstallRequest role_request;
  {
    std::lock_guard<std::mutex> lock(g_pull_mutex);
    if (g_pull_job.handle.id != handle.id) {
      return {false, false, "ota_pull_context_stale"};
    }
    url = g_pull_job.url;
    sha256 = g_pull_job.expected_sha256;
    size = g_pull_job.expected_size;
    role_aware=g_pull_job.role_aware;role_request=g_pull_job.role_request;
  }

  ota_service_report_pull_started(handle.id);
  std::string preparation_error;
  const bool prepared = ota_service_prepare_maintenance(preparation_error);
  if (!prepared) ota_service_report_failure(preparation_error.c_str());
  const bool installed = prepared && install_from_url(url,sha256,size,handle.id,role_aware?&role_request:nullptr);
  const OtaServiceStatus status = ota_service_status_snapshot();
  const bool retryable = !installed && retryable_pull_failure(status.message) &&
                         retry_attempt_available(handle);
  if (retryable) {
    ota_service_report_pull_queued(handle.id);
  } else {
    ota_service_report_pull_finished(handle.id);
    std::lock_guard<std::mutex> lock(g_pull_mutex);
    if (g_pull_job.handle.id == handle.id) {
      g_pull_job = {};
    }
    ota_service_release_pull();
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
    if(!role_aware)ota_intent_finish(sha256, installed);
#endif
    if (!installed) {
      ota_service_release_maintenance();
    }
  }
  return {installed, retryable,
          installed ? "ota_pull_complete" : status.message};
}

} // namespace

static bool queue_install_from_https(const std::string &url,
                                          const std::string &expected_sha256,
                                          size_t expected_size,
                                          uint32_t &job_id,
                                          std::string &error, bool resuming = false,
                                          const OtaCloudInstallRequest *role_request = nullptr) {
  job_id = 0;
  error.clear();
  size_t partition_capacity = ota_writer_metadata().next.size;
  if(role_request&&role_request->target_role=="boot"&&ota_cloud_recovery_layout(role_request->layout_id))partition_capacity=0x200000;
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  const auto recovery = ota_recovery_snapshot();
  if (!recovery.ready) { error = "recovery_state_unavailable"; return false; }
  if (!recovery.recovery_running && !role_request)
    partition_capacity = ota_service_deployment_snapshot().business_slot_bytes;
#else
  (void)resuming;
#endif
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

#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (!recovery.recovery_running && !role_request) {
    // The request owns stream admission and preparation atomically. A busy
    // rejection must never release another upload's maintenance window.
    if (!ota_intent_stage({url, expected_sha256, expected_size}, error) ||
        !ota_service_request_recovery(error)) {
      ota_intent_finish(expected_sha256, false);
      return false;
    }
    return true; // 202: durable intent, then factory resumes after restart.
  }
#endif
  if (!ota_service_reserve_pull(error)) return false;
  if (!ota_service_prepare_maintenance(error)) {
    ota_service_release_maintenance();
    ota_service_release_pull();
    return false;
  }
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  if (!role_request && !resuming && (!ota_intent_stage({url, expected_sha256, expected_size}, error) ||
      !ota_recovery_stage_install_resume(error))) {
    ota_intent_finish(expected_sha256, false);
    ota_service_release_maintenance();
    ota_service_release_pull();
    return false;
  }
#endif
  g_pull_job.url = url;
  g_pull_job.expected_sha256 = expected_sha256;
  g_pull_job.expected_size = expected_size;
  g_pull_job.role_aware=role_request!=nullptr;
  if(role_request)g_pull_job.role_request=*role_request;
  lunaredu::lunaros::NetworkJobHandle handle;
  NetworkJobDefinition definition{
      "system.update", "firmware_pull", kNetworkPriorityCritical, 3, 0,
      run_ota_pull, nullptr, false, 5000, 5000};
  definition.job_class = NetworkJobClass::Ota;
  if (!submit_network_job(definition, handle, error)) {
    g_pull_job = {};
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
    if(!role_request)ota_intent_finish(expected_sha256, false);
#endif
    ota_service_release_maintenance();
    ota_service_release_pull();
    return false;
  }
  g_pull_job.handle = handle;
  job_id = handle.id;
  ota_service_report_pull_queued(job_id);
  return true;
}

bool ota_service_queue_install_from_https(const std::string &url,
    const std::string &sha256, size_t size, uint32_t &job_id, std::string &error) {
  return queue_install_from_https(url, sha256, size, job_id, error);
}

bool ota_service_queue_role_install_from_https(const OtaCloudInstallRequest &request,
    uint32_t &job_id,std::string &error) {
  job_id=0;
  const auto inventory=ota_service_cloud_inventory();
  if(!ota_cloud_request_allowed(request,inventory,error))return false;
  if(request.target_role=="boot"&&inventory.running_role=="boot"){
    error="ota_boot_requires_application";return false;
  }
  if(request.target_role=="application"&&inventory.running_role=="application"){
    // PlatformLink committed the exact role intent first. Boot obtains a fresh
    // ticket for that intent; never leave a legacy URL-only replay behind.
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
    if(!ota_intent_clear(error))return false;
#endif
    return ota_service_request_recovery(error);
  }
  return queue_install_from_https(request.url,request.sha256,request.size,job_id,error,false,&request);
}

void ota_service_tick_recovery_install() {
  NetworkJobHandle abandoned;
  std::string abandoned_sha;
  [[maybe_unused]] bool abandoned_role_aware=false;
  {
    std::lock_guard<std::mutex> lock(g_pull_mutex);
    if (g_pull_job.handle && !network_job_active(g_pull_job.handle)) {
      abandoned = g_pull_job.handle;
      abandoned_sha = g_pull_job.expected_sha256;
      abandoned_role_aware=g_pull_job.role_aware;
      g_pull_job = {};
    }
  }
  if (abandoned) {
    ota_service_report_pull_finished(abandoned.id);
    ota_service_release_pull();
    ota_service_report_failure("ota_pull_cancelled_before_dispatch");
    ota_service_release_maintenance();
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
    if(!abandoned_role_aware)ota_intent_finish(abandoned_sha, false);
#endif
  }
#if CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT
  static uint32_t next_attempt_ms = 0;
  const uint32_t now = system_monotonic_ms();
  if (static_cast<int32_t>(now - next_attempt_ms) < 0) return;
  next_attempt_ms = now + 5000;
  const auto recovery = ota_recovery_snapshot();
  const auto status = ota_service_status_snapshot();
  if (!recovery.ready || !recovery.recovery_running || status.upload_active ||
      status.pull_active || status.pull_queued || status.restart_pending) return;
  // Only an explicitly staged request or interrupted write may resume.
  // A failed/confirmed business boot is a rescue, never a reason to install
  // the old authorized URL again (which may still contain a valid ticket).
  if (recovery.phase != "requested" && recovery.phase != "failed") return;
  OtaInstallIntent intent;
  std::string error;
  if (!ota_intent_begin_attempt(intent, error)) {
    if (!error.empty()) ota_service_report_failure(error.c_str());
    return;
  }
  uint32_t job_id = 0;
  if (!queue_install_from_https(intent.url, intent.sha256, intent.size, job_id, error, true)) {
    ota_service_report_failure(error.c_str());
    // Synchronous preparation/admission failures retain the committed bounded
    // attempt count; subsequent ticks may retry when startup becomes ready.
  }
#endif
}

} // namespace lunaredu::lunaros
