#include "network/http/http_request_internal.h"
#include "lunaros_network_jobs.h"
#include "lunaros_network_tls.h"
#include "network/http/tls_session_cache.h"
#include "network/http/http_connection_cache.h"
#include <strings.h>
#include "diagnostics/cpu_monitor.h"
#include "kernel/bounded_work.h"

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/error.h>
#include <mbedtls/x509_crt.h>

#include <cstdio>
#include <cerrno>
#include <algorithm>
#include <utility>
#include <array>
#include <climits>
#include <atomic>

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
      : request_(std::move(request)), deadline_us_(request_.total_timeout_ms>0?
          esp_timer_get_time()+int64_t(request_.total_timeout_ms)*1000:INT64_MAX) {}
  ~EspHttpRequestStream() override { Close(); }

  static std::unique_ptr<NetworkRequestStream>
  create(NetworkRequest request, int *failure_error,
         std::string *failure_error_detail, bool *failure_deferred) {
    if (failure_deferred != nullptr) *failure_deferred = false;
    auto stream = std::unique_ptr<EspHttpRequestStream>(
        new EspHttpRequestStream(std::move(request)));
    if (!stream->begin()) {
      if (failure_error != nullptr) {
        *failure_error = stream->GetLastError();
      }
      if (failure_error_detail != nullptr) {
        *failure_error_detail = stream->GetLastErrorDetail();
      }
      if (failure_deferred != nullptr) *failure_deferred = stream->deferred_;
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
    if (!continue_request()) return -1;
    if (!ensure_headers_fetched()) {
      return -1;
    }
    if (client_ == nullptr || response_complete_) {
      return 0;
    }
    BoundedWorkScope bounded_work(uint32_t(esp_timer_get_time() / 1000), uint32_t(std::max(request_.timeout_ms, 1)));
    network_job_runner_set_phase(NetworkJobPhase::ResponseBody);
    const int64_t started_us = esp_timer_get_time();
    int read_size;
    for (;;) {
      if (!prepare_io(true)) return -1;
      errno = 0;
      read_size = esp_http_client_read(client_, buffer, static_cast<int>(buffer_size));
      if (!continue_request()) return -1;
      if (read_size > 0 || !would_block(read_size)) break;
      if (!wait_for_progress(started_us)) { capture_http_client_error("read"); return -1; }
    }
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
  std::string GetHeader(const std::string &name) override {
    for(const auto &header:response_headers_)
      if(!strcasecmp(header.key.c_str(),name.c_str()))return header.value;
    return {};
  }
  bool RequestCancel() override {
    cancellation_requested_.store(true, std::memory_order_release);
    return true;
  }
  void Close() override {
    BoundedWorkScope bounded_cleanup(uint32_t(esp_timer_get_time() / 1000), uint32_t(std::max(request_.timeout_ms, 1)));
    if (client_ == nullptr) {
      release_http_tls_session(tls_session_, false);
      release_http_connection(connection_);
      return;
    }
    const int64_t cleanup_started = esp_timer_get_time();
    const uint32_t cleanup_task = request_task_counter();
    network_job_runner_set_phase(NetworkJobPhase::ResourceCleanup);
    const bool session_offered = tls_session_.offered;
    bool session_saved = false;
    const bool retained=opened_&&last_error_==0&&headers_fetched_&&response_complete_&&
      status_code_>=200&&status_code_<300&&
      esp_http_client_is_complete_data_received(client_)&&
      esp_http_client_is_persistent_connection(client_)&&
      retain_http_connection(connection_,client_,tls_session_);
    if(!retained) {
    // Export a TLS ticket only when releasing the live connection. Saving
    // it on every warm HTTP response would repeat the SDK's session copy.
    session_saved = opened_ && last_error_ == 0 && headers_fetched_ &&
        esp_http_client_is_complete_data_received(client_) &&
        save_http_tls_session(tls_session_);
    if (opened_) {
      esp_http_client_close(client_);
      opened_ = false;
    }
    esp_http_client_cleanup(client_);
    client_ = nullptr;
    release_http_tls_session(tls_session_, session_saved);
    release_http_connection(connection_);
    }
    opened_=false;
    const int64_t finished = esp_timer_get_time();
    if (profile_task_) {
      const uint32_t finished_task = request_task_counter();
      const uint32_t setup_task = opened_us_ ? opened_task_ : cleanup_task;
      const uint32_t headers_task = headers_us_ ? headers_task_ : cleanup_task;
      // Fits the bounded USB sink; session flags would otherwise be truncated.
      ESP_LOGI("network_http", "HTTP_PROFILE basis=task reuse=%d session=%d saved=%d setup_ms=%lld headers_ms=%lld body_ms=%lld cleanup_ms=%lld setup_task_ms=%lu headers_task_ms=%lu body_task_ms=%lu cleanup_task_ms=%lu status=%d error=%d",
          reused_connection_,session_offered,session_saved,
          ((opened_us_ ? opened_us_ : cleanup_started) - started_us_) / 1000,
          headers_us_ && opened_us_ ? (headers_us_ - opened_us_) / 1000 : 0,
          headers_us_ ? (cleanup_started - headers_us_) / 1000 : 0,
          (finished - cleanup_started) / 1000,
          static_cast<unsigned long>(uint32_t(setup_task - started_task_) / 1000),
          static_cast<unsigned long>(opened_us_ ? uint32_t(headers_task - opened_task_) / 1000 : 0),
          static_cast<unsigned long>(headers_us_ ? uint32_t(cleanup_task - headers_task_) / 1000 : 0),
          static_cast<unsigned long>(uint32_t(finished_task - cleanup_task) / 1000),status_code_,last_error_);
    }
    if (started_us_ && finished - started_us_ >= 2000000) {
      // Durations only: URLs, headers and request bodies may contain secrets.
      ESP_LOGW("network_http", "slow request setup_ms=%lld headers_ms=%lld body_ms=%lld cleanup_ms=%lld status=%d error=%d",
          (opened_us_ ? opened_us_ : cleanup_started) - started_us_ > 0
              ? ((opened_us_ ? opened_us_ : cleanup_started) - started_us_) / 1000 : 0,
          headers_us_ && opened_us_ ? (headers_us_ - opened_us_) / 1000 : 0,
          headers_us_ ? (cleanup_started - headers_us_) / 1000 : 0,
          (finished - cleanup_started) / 1000, status_code_, last_error_);
    }
  }

private:
  // Only the I/O owner touches SDK state. A late stop rejects publication but
  // never asserts that a request already sent had no server-side effect.
  bool continue_request() {
    if (cancellation_requested_.load(std::memory_order_acquire) ||
        request_.cancellation.stop_requested() || network_job_runner_cancellation_requested()) {
      last_error_ = ESP_ERR_INVALID_STATE;
      last_error_detail_ = connected_||opened_||request_complete_?
        "request_cancelled_completion_unconfirmed":"request_cancelled_before_send";
      return false;
    }
    if (esp_timer_get_time() >= deadline_us_ || network_job_runner_remaining_timeout_ms(1)==0) {
      last_error_ = ESP_ERR_TIMEOUT;
      last_error_detail_ = "request_deadline_exceeded";
      return false;
    }
    return last_error_ == 0;
  }
  bool prepare_io(bool resumable_read=false,int maximum_wait_ms=INT_MAX) {
    if (!continue_request()) return false;
    const auto remaining = network_job_runner_remaining_timeout_ms(uint32_t(std::max<int64_t>(1, std::min<int64_t>(request_.timeout_ms,
        (deadline_us_ - esp_timer_get_time()) / 1000))));
    if(!remaining){last_error_=ESP_ERR_TIMEOUT;last_error_detail_="request_deadline_exceeded";return false;}
    // IDF read() preserves partial bytes and returns HTTP_EAGAIN on a socket
    // wait timeout. Only that resumable boundary gets a short wait slice;
    // writes/header setup retain their timeout and are never replayed.
    const auto timeout=std::min<uint32_t>(resumable_read?std::min<uint32_t>(remaining,50):remaining,
        uint32_t(maximum_wait_ms));
    const auto result = esp_http_client_set_timeout_ms(client_, int(std::min<int64_t>(INT_MAX, timeout)));
    if (result != ESP_OK) { last_error_ = result; return false; }
    return true;
  }
  uint32_t request_task_counter() const {
#if configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
    if (profile_task_) {
      TaskStatus_t task{};
      // No registry scan, stack watermark scan, allocation or new worker.
      // The current unswitched slice is excluded by the SDK counter; these
      // diagnostic deltas are attribution evidence, not precise cycle totals.
      vTaskGetInfo(nullptr,&task,pdFALSE,eInvalid);
      return static_cast<uint32_t>(task.ulRunTimeCounter);
    }
#endif
    return 0;
  }
  static esp_err_t on_event(esp_http_client_event_t *event) {
    if(!event->user_data)return ESP_OK;
    auto &stream=*static_cast<EspHttpRequestStream *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_CONNECTED) {
      network_job_runner_set_phase(NetworkJobPhase::RequestSend);
      // SDK emits this before sending headers. From this boundary onwards a
      // request may be written inside open(), so yielding is no longer safe.
      stream.connected_ = true;
    } else if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
      for(const auto &key:stream.request_.response_header_keys) {
        if(strcasecmp(key.c_str(),event->header_key))continue;
        if(std::char_traits<char>::length(event->header_value)>1024){
          stream.last_error_=ESP_ERR_INVALID_SIZE;return ESP_ERR_INVALID_SIZE;
        }
        for(auto &header:stream.response_headers_)if(!strcasecmp(header.key.c_str(),key.c_str())){
          header.value=event->header_value;return ESP_OK;
        }
        stream.response_headers_.push_back({key,event->header_value});break;
      }
    }
    return ESP_OK;
  }
  bool yield_transport_setup() {
    if (connected_ || !network_job_runner_should_yield_transport_setup()) return false;
    last_error_ = ESP_ERR_INVALID_STATE;
    last_error_detail_ = "transport_setup_yielded";
    deferred_ = true;
    return true;
  }
  bool begin(bool fresh_only = false) {
    if (!continue_request()) return false;
    profile_task_=lunaredu::system::worker_work_profiling_active();
    started_task_=request_task_counter();
    started_us_ = esp_timer_get_time();
    network_job_runner_set_phase(NetworkJobPhase::TransportSetup);
    if (yield_transport_setup()) return false;
    const bool plaintext=request_.transport_security==NetworkTransportSecurity::ExplicitHttp;
    {
    // Cache retirement and SDK initialization can also perform blocking SDK
    // cleanup. They belong to transport setup, before open()'s own deadline.
    BoundedWorkScope bounded_setup(uint32_t(esp_timer_get_time() / 1000), uint32_t(std::max(request_.timeout_ms, 1)));
    esp_http_client_config_t config = {};
    config.url = request_.url.c_str();
    // ESP-TLS async connect still uses this value in a blocking socket
    // select(). Slice that wait so the runner can yield to retirement.
    // wait_for_progress() retains the request's complete setup deadline.
    const auto setup_budget=network_job_runner_remaining_timeout_ms(uint32_t(std::max(request_.timeout_ms,1)));
    const auto total_budget=std::max<int64_t>(1,(deadline_us_-esp_timer_get_time())/1000);
    const auto setup_timeout=int(std::min<int64_t>(setup_budget,total_budget));
    if(setup_timeout<=0){last_error_=ESP_ERR_TIMEOUT;last_error_detail_="request_deadline_exceeded";return false;}
    config.timeout_ms = plaintext?setup_timeout:std::min(setup_timeout,20);
    config.keep_alive_enable = request_.keep_alive;
    tls_policy_ = request_.system_trust_only
        ? std::make_shared<const NetworkTlsPolicy>()
        : network_tls_policy_for_url(request_.url);
    if(request_.response_header_keys.size()>8){last_error_=ESP_ERR_INVALID_SIZE;return false;}
    if(!fresh_only)connection_=acquire_http_connection(request_,tls_policy_);
    network_job_runner_set_phase(NetworkJobPhase::TransportSetup);
    client_=connection_.client;tls_session_=connection_.tls;
    reused_connection_=client_!=nullptr;
    if (tls_policy_->mode == "custom_ca") config.cert_pem = tls_policy_->ca_pem.c_str();
    else config.crt_bundle_attach = tls_policy_->mode == "skip_verify"
        ? network_tls_skip_verification_attach : esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.event_handler = on_event;
    config.user_data = this;
    // The SDK's synchronous TLS handshake retries WANT_READ/WANT_WRITE in
    // a tight loop. Drive its supported async state machine on this existing
    // I/O owner, sleeping on retry so the idle task and resident audio can run.
    // ESP-IDF async connect supports SSL, not plain TCP. Explicit HTTP runs
    // on the same bounded I/O owner with its original socket timeout.
    config.is_async = !plaintext;
    config.buffer_size = 2048;
    config.buffer_size_tx = 2048;
    if (!fresh_only && !client_)
      tls_session_ = acquire_http_tls_session(request_.url, tls_policy_, request_.keep_alive);
#if CONFIG_ESP_HTTP_CLIENT_ENABLE_CUSTOM_TRANSPORT && CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
    config.transport = tls_session_.transport;
    // The lease owns session SAVE/USE; the per-request client never owns it.
    config.save_client_session = false;
#endif
    if(client_) {
      if(esp_http_client_set_user_data(client_,this)!=ESP_OK ||
          esp_http_client_set_url(client_,request_.url.c_str())!=ESP_OK ||
          esp_http_client_set_timeout_ms(client_,config.timeout_ms)!=ESP_OK){
        last_error_=ESP_FAIL;return false;
      }
      // A reused socket does not emit ON_CONNECTED again; open() still may
      // send headers. Never yield/replay it as an unsent handshake.
      connected_=true;
      network_job_runner_set_phase(NetworkJobPhase::RequestSend);
    } else client_ = esp_http_client_init(&config);
    if (client_ == nullptr) {
      last_error_ = ESP_FAIL;
      return false;
    }

    if(esp_http_client_set_method(client_, to_http_method(request_.method))!=ESP_OK){last_error_=ESP_FAIL;return false;}
    for (const auto &header : request_.headers) {
      if(esp_http_client_set_header(client_, header.key.c_str(),
                                 header.value.c_str())!=ESP_OK){last_error_=ESP_FAIL;return false;}
    }
    }

    const auto body_size=request_.body_stream.read?request_.body_stream.length:(request_.body?request_.body->size():0);
    if(body_size>INT_MAX || (request_.body_stream.read && request_.body.has_value())){
      last_error_=ESP_ERR_INVALID_ARG;last_error_detail_="invalid_request_body_stream";return false;
    }
    const int write_len=static_cast<int>(body_size);
    const int64_t started_us = esp_timer_get_time();
    esp_err_t open_error;
    {
    BoundedWorkScope bounded_work(uint32_t(started_us / 1000), uint32_t(std::max(request_.timeout_ms, 1)));
    do {
      if (!continue_request()) { Close(); return false; }
      if (yield_transport_setup()) { Close(); return false; }
      // Keep the original setup slice, additionally capped by the remaining
      // request/job budget on every async continuation.
      if(!prepare_io(false,plaintext?INT_MAX:20)) { Close();return false; }
      errno = 0;
      open_error = esp_http_client_open(client_, write_len);
      if (open_error != ESP_ERR_HTTP_EAGAIN) break;
      if (!wait_for_progress(started_us)) { open_error = last_error_; break; }
    } while (true);
    }
    if (open_error != ESP_OK) {
      last_error_ = open_error;
      capture_http_client_error("open");
      // One verified full handshake can recover a stale resumption, only
      // before ON_CONNECTED (open() can write HTTP headers after that event).
      // No HTTP request replay, no fresh deadline or retries after any send.
      const bool recover_session = tls_session_.offered && !connected_ && !fresh_only;
      Close();
      const auto elapsed_ms = (esp_timer_get_time() - started_us_) / 1000;
      const auto remaining_ms = request_.timeout_ms - elapsed_ms;
      if (recover_session && remaining_ms > 0 && !network_job_runner_should_yield_transport_setup()) {
        request_.timeout_ms = static_cast<int>(remaining_ms);
        last_error_ = 0;
        last_error_detail_.clear();
        opened_us_ = headers_us_ = 0;
        opened_task_ = headers_task_ = 0;
        return begin(true);
      }
      return false;
    }
    opened_ = true;
    if (!continue_request()) { Close(); return false; }
    opened_us_ = esp_timer_get_time();
    opened_task_=request_task_counter();
    if (!prepare_io()) {
      capture_http_client_error("restore_timeout");
      Close();
      return false;
    }

    if (request_.body.has_value() && !request_.body->empty() &&
        !write_raw_all(request_.body->data(), request_.body->size())) {
      capture_http_client_error("write_body");
      Close();
      return false;
    }
    if(request_.body_stream.read){
      std::array<uint8_t,2048> chunk{};size_t remaining=request_.body_stream.length;
      while(remaining){
        if (!continue_request()) return false;
        const auto count=request_.body_stream.read(chunk.data(),std::min(chunk.size(),remaining),request_.body_stream.context);
        if(count<=0 || size_t(count)>std::min(chunk.size(),remaining)){last_error_=ESP_ERR_INVALID_SIZE;last_error_detail_="request_body_incomplete";return false;}
        if(!write_raw_all(reinterpret_cast<const char*>(chunk.data()),size_t(count))){capture_http_client_error("write_body");return false;}
        remaining-=size_t(count);
      }
    }
    request_complete_ = true;
    return ensure_headers_fetched();
  }

  bool ensure_headers_fetched() {
    if (!continue_request()) return false;
    if (headers_fetched_) {
      return true;
    }
    if (client_ == nullptr || !request_complete_) {
      last_error_ = ESP_FAIL;
      return false;
    }
    BoundedWorkScope bounded_work(uint32_t(esp_timer_get_time() / 1000), uint32_t(std::max(request_.timeout_ms, 1)));
    const int64_t started_us = esp_timer_get_time();
    int64_t fetch_result;
    network_job_runner_set_phase(NetworkJobPhase::ResponseHeaders);
    for (;;) {
      if (!prepare_io()) return false;
      errno = 0;
      fetch_result = esp_http_client_fetch_headers(client_);
      if (!continue_request()) return false;
      if (fetch_result >= 0 || !would_block(fetch_result)) break;
      if (!wait_for_progress(started_us)) { capture_http_client_error("fetch_headers"); return false; }
    }
    if (fetch_result < 0) {
      last_error_ = static_cast<int>(fetch_result);
      capture_http_client_error("fetch_headers");
      return false;
    }
    status_code_ = esp_http_client_get_status_code(client_);
    content_length_ = esp_http_client_get_content_length(client_);
    if(last_error_!=0)return false;
    headers_fetched_ = true;
    headers_us_ = esp_timer_get_time();
    headers_task_=request_task_counter();
    return true;
  }

  bool write_raw_all(const char *buffer, size_t buffer_size) {
    BoundedWorkScope bounded_work(uint32_t(esp_timer_get_time() / 1000), uint32_t(std::max(request_.timeout_ms, 1)));
    size_t total_written = 0;
    const int64_t started_us = esp_timer_get_time();
    while (total_written < buffer_size) {
      if (!prepare_io()) return false;
      errno = 0;
      const int written = esp_http_client_write(
          client_, buffer + total_written,
          static_cast<int>(buffer_size - total_written));
      if (!continue_request()) return false;
      if (written <= 0) {
        if (would_block(written)) {
          if (wait_for_progress(started_us)) continue;
        } else last_error_ = written < 0 ? written : ESP_FAIL;
        capture_http_client_error("write");
        return false;
      }
      total_written += static_cast<size_t>(written);
    }
    return true;
  }

  static bool would_block(int64_t result) {
    return result == -ESP_ERR_HTTP_EAGAIN || errno == EAGAIN || errno == EWOULDBLOCK;
  }
  bool wait_for_progress(int64_t started_us) {
    if (!continue_request()) return false;
    if (request_.timeout_ms <= 0 || esp_timer_get_time() - started_us >=
        static_cast<int64_t>(request_.timeout_ms) * 1000) {
      last_error_ = ESP_ERR_TIMEOUT;
      return false;
    }
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(1)));
    return true;
  }

  void capture_http_client_error(const char *stage) {
    if (!last_error_detail_.empty()) return;
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
    detail += " dma_free=" + std::to_string(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    detail += " dma_largest=" + std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    detail += " transport=" + std::to_string(static_cast<int>(transport));
    detail += " err=" + format_esp_error(last_error_);
    detail += " tls_result=" + format_esp_error(tls_error);
    detail += " tls_code=" + format_mbed_tls_error(tls_code);
    detail += " tls_flags=" + format_verify_flags(tls_flags);
    detail += " errno=" + std::to_string(socket_error);
    last_error_detail_ = std::move(detail);
  }

  NetworkRequest request_;
  const int64_t deadline_us_;
  std::atomic<bool> cancellation_requested_{false};
  std::shared_ptr<const NetworkTlsPolicy> tls_policy_;
  esp_http_client_handle_t client_ = nullptr;
  HttpTlsSessionLease tls_session_;
  HttpConnectionLease connection_;
  std::vector<NetworkHttpHeader> response_headers_;
  bool reused_connection_=false;
  bool opened_ = false;
  bool connected_ = false;
  bool deferred_ = false;
  bool request_complete_ = false;
  bool headers_fetched_ = false;
  bool response_complete_ = false;
  int status_code_ = -1;
  int64_t content_length_ = 0;
  int last_error_ = 0;
  int64_t started_us_ = 0;
  int64_t opened_us_ = 0;
  int64_t headers_us_ = 0;
  bool profile_task_ = false;
  uint32_t started_task_=0,opened_task_=0,headers_task_=0;
  std::string last_error_detail_;
};

} // namespace

std::unique_ptr<NetworkRequestStream>
create_esp_http_request_stream(NetworkRequest request, int *failure_error,
                               std::string *failure_error_detail, bool *failure_deferred) {
  return EspHttpRequestStream::create(std::move(request), failure_error,
                                      failure_error_detail, failure_deferred);
}

} // namespace lunaredu::lunaros
