#include "network/http/tls_session_cache.h"
#include <esp_http_client.h>
#include <atomic>
namespace lunaredu::lunaros {
namespace { std::atomic<uint64_t> policy_revision{1}; }
uint64_t http_tls_policy_generation() { return policy_revision.load(); }
}
#if CONFIG_ESP_HTTP_CLIENT_ENABLE_CUSTOM_TRANSPORT && CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
#include "kernel/static_mutex.h"
#include "lunaros_network_jobs.h"
#include "lunaros_network_service_id.h"
#include <esp_transport_ssl.h>
#include <esp_crt_bundle.h>
#include <esp_timer.h>
#include <atomic>
#include <algorithm>
#include <utility>

namespace lunaredu::lunaros {
namespace {
constexpr int64_t kIdleUs = 120000000;
constexpr int64_t kLifetimeUs = 600000000;
StaticMutex mutex;
esp_transport_handle_t transport = nullptr;
std::string origin;
std::shared_ptr<const NetworkTlsPolicy> trust;
esp_transport_keep_alive_t keepalive{};
enum class Phase { Idle, Acquiring, Leased, Retiring };
Phase phase = Phase::Idle;
bool invalidated = false;
uint64_t generation = 0, next_generation = 1;
int64_t created_us = 0;
std::atomic<int64_t> expires_us{0};
struct RetiredSession {
  esp_transport_handle_t transport = nullptr;
  std::shared_ptr<const NetworkTlsPolicy> trust;
};
RetiredSession detach_locked() {
  RetiredSession retired{transport, std::move(trust)};
  transport = nullptr;
  origin.clear();
  expires_us.store(0);
  invalidated = false;
  return retired;
}
void destroy_retired(RetiredSession retired) {
  // CA bytes stay owned until the SDK is finished. The slot remains reserved
  // while callbacks, readers and competing acquisition bypass this cleanup.
  if (retired.transport) esp_transport_destroy(retired.transport);
}
void finish_retirement() {
  StaticLockGuard lock(mutex);
  phase = Phase::Idle;
  invalidated = false;
}
bool current_lease(HttpTlsSessionLease lease) {
  return phase == Phase::Leased && lease.transport &&
      lease.transport == transport && lease.generation == generation;
}
bool same_trust(const NetworkTlsPolicy &a, const NetworkTlsPolicy &b) {
  return a.mode == b.mode && a.ca_pem == b.ca_pem && a.origin == b.origin;
}
}
HttpTlsSessionLease acquire_http_tls_session(const std::string &url,
    std::shared_ptr<const NetworkTlsPolicy> policy, bool keep_alive) {
  if (!policy || policy->mode == "skip_verify" || url.rfind("https://", 0) != 0)
    return {};
  std::string destination;
  const auto end = url.find_first_of("/?#", 8);
  if (!network_tls_normalize_origin(url.substr(0, end), destination)) return {};
  const auto now = esp_timer_get_time();
  RetiredSession retired;
  esp_transport_handle_t acquired;
  uint64_t ticket;
  {
    StaticLockGuard lock(mutex);
    if (phase != Phase::Idle || next_generation == 0) return {};
    phase = Phase::Acquiring;
    ticket = next_generation++;
    if (transport && (invalidated || now >= expires_us.load() ||
        origin != destination || !same_trust(*trust, *policy))) retired = detach_locked();
    acquired = transport;
  }
  destroy_retired(std::move(retired));
  const bool created = !acquired;
  if (created) {
    acquired = esp_transport_ssl_init();
    if (!acquired) { finish_retirement(); return {}; }
    if (policy->mode == "custom_ca")
      esp_transport_ssl_set_cert_data(acquired, policy->ca_pem.c_str(), policy->ca_pem.size());
    else esp_transport_ssl_crt_bundle_attach(acquired, esp_crt_bundle_attach);
  }
  keepalive = {keep_alive, 5, 5, 3};
  esp_transport_ssl_set_keep_alive(acquired, &keepalive);
  const bool offered = esp_transport_ssl_session_ticket_operation(
      acquired, ESP_TRANSPORT_SESSION_TICKET_USE) == ESP_OK;
  {
    StaticLockGuard lock(mutex);
    transport = acquired;
    if (created) {
      origin = std::move(destination); trust = std::move(policy); created_us = now;
    }
    generation = ticket; phase = Phase::Leased;
    expires_us.store(std::min(now + kIdleUs, created_us + kLifetimeUs));
  }
  return {acquired, offered, ticket};
}
bool save_http_tls_session(HttpTlsSessionLease lease) {
  // Called on the exclusive I/O owner while TLS is still alive, after the
  // complete response (TLS 1.3 tickets may arrive after the handshake).
  { StaticLockGuard lock(mutex); if (!current_lease(lease)) return false; }
  if (esp_transport_ssl_session_ticket_operation(lease.transport,
      ESP_TRANSPORT_SESSION_TICKET_SAVE) != ESP_OK) return false;
  return esp_transport_ssl_session_ticket_operation(lease.transport,
      ESP_TRANSPORT_SESSION_TICKET_USE) == ESP_OK;
}
void release_http_tls_session(HttpTlsSessionLease &lease, bool saved) {
  if (!lease.transport) return;
  {
    StaticLockGuard lock(mutex);
    if (!current_lease(lease)) { lease = {}; return; }
    phase = Phase::Retiring;
  }
  // HTTP cleanup may leave an async handshake open when init/open failed.
  // Keep the reservation, not the cache mutex, across SDK teardown.
  esp_transport_close(lease.transport);
  RetiredSession retired;
  {
    StaticLockGuard lock(mutex);
    const auto now = esp_timer_get_time();
    const auto deadline=expires_us.load();
    if (!saved || invalidated || now >= created_us + kLifetimeUs ||
        (deadline&&now>=deadline)) retired = detach_locked();
    else {
      expires_us.store(std::min(now + kIdleUs, created_us + kLifetimeUs));
      phase = Phase::Idle;
    }
  }
  if (retired.transport) { destroy_retired(std::move(retired)); finish_retirement(); }
  lease = {};
}
void invalidate_http_tls_session() {
  ++policy_revision;
  RetiredSession retired;
  {
    StaticLockGuard lock(mutex);
    if (phase != Phase::Idle) { invalidated = true; return; }
    if (!transport) return;
    phase = Phase::Retiring; retired = detach_locked();
  }
  destroy_retired(std::move(retired)); finish_retirement();
}
void retire_expired_http_tls_session() {
  const auto deadline = expires_us.load();
  if (!deadline || esp_timer_get_time() < deadline) return;
  RetiredSession retired;
  {
    StaticLockGuard lock(mutex);
    if (!expires_us.load() || esp_timer_get_time() < expires_us.load()) return;
    if (phase != Phase::Idle) { invalidated = true; expires_us.store(0); return; }
    phase = Phase::Retiring; retired = detach_locked();
  }
  destroy_retired(std::move(retired)); finish_retirement();
}
// Only the network maintenance owner calls this submission path. Transport
// retirement itself runs on the existing blocking-I/O executor. A delayed or
// cancelled job never owns a lease; its runner rechecks expiration under the
// cache lock, so renewed/replaced sessions cannot be destroyed accidentally.
void poll_http_tls_session() {
  const auto deadline=expires_us.load();
  const auto now=esp_timer_get_time();
  if(!deadline||now<deadline)return;
  static NetworkJobHandle retirement;
  static int64_t retry_at=0;
  if(now<retry_at||network_job_active(retirement))return;
  const NetworkJobDefinition job{.owner_service_id=kNetworkRuntimeServiceId,
    .kind="tls_session_retirement",.priority=kNetworkPriorityMaintenance,
    .runner=[](NetworkJobHandle,void *){retire_expired_http_tls_session();return NetworkJobRunResult{true,false,{}};},
    .job_class=NetworkJobClass::Retirement};
  std::string error;
  if(!submit_network_job(job,retirement,error))retry_at=now+1'000'000;
  else retry_at=0;
}
} // namespace lunaredu::lunaros
#else
namespace lunaredu::lunaros {
HttpTlsSessionLease acquire_http_tls_session(const std::string &,
    std::shared_ptr<const NetworkTlsPolicy>, bool) { return {}; }
bool save_http_tls_session(HttpTlsSessionLease) { return false; }
void release_http_tls_session(HttpTlsSessionLease &lease, bool) { lease = {}; }
void invalidate_http_tls_session() { ++policy_revision; }
void poll_http_tls_session() {}
}
#endif
