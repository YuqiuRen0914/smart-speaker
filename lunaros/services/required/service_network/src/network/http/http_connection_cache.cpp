#include "network/http/http_connection_cache.h"
#include "kernel/static_mutex.h"
#include "lunaros_network_jobs.h"
#include "lunaros_network_service_id.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <cerrno>
#include <algorithm>
#include <atomic>
#include <utility>

namespace lunaredu::lunaros {
namespace {
constexpr int64_t kIdleUs = 15'000'000, kLifetimeUs = 90'000'000;
// Warm TLS is expendable storage: leave room for the internal I/O stack and
// a new real-time claimant. These are shared resource floors, not scene rules.
constexpr size_t kMinimumFree = 32 * 1024, kMinimumBlock = 16 * 1024;
StaticMutex mutex;
struct Slot {
  HttpConnectionLease handles;
  std::shared_ptr<const NetworkTlsPolicy> trust;
  std::string origin;
  std::string scope;
  std::vector<NetworkHttpHeader> headers;
  uint64_t generation = 0, policy_generation = 0;
  int64_t created_us = 0;
  bool leased = false, retiring = false;
};
// This small owner must retain its C++ constant initialization. In particular,
// empty std::strings contain pointers to their own inline buffers. Placing it
// in EXT_RAM_BSS_ATTR loses those initialized pointers in the NOLOAD section;
// the compiler need not emit a dynamic constructor to recreate them.
Slot slot;
uint64_t next_generation = 1;
std::atomic<int64_t> expires_us{0};
bool room() {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) >= kMinimumFree &&
    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) >= kMinimumBlock;
}
bool same_headers(const std::vector<NetworkHttpHeader> &a,
                  const std::vector<NetworkHttpHeader> &b) {
  if(a.size()!=b.size())return false;
  for(size_t i=0;i<a.size();++i)if(a[i].key!=b[i].key||a[i].value!=b[i].value)return false;
  return true;
}
bool known_closed(esp_http_client_handle_t client) {
  const int socket=esp_http_client_get_socket(client);
  if(socket<0)return true;
  char byte;
  // Observe TCP EOF/error without consuming encrypted bytes, waiting, or
  // entering TLS. Only this lease's I/O owner can touch the borrowed socket.
  const auto read=recv(socket,&byte,1,MSG_PEEK|MSG_DONTWAIT);
  return read==0 || (read<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR);
}
void destroy(HttpConnectionLease handles) {
  if(!handles.client)return;
  network_job_runner_set_phase(NetworkJobPhase::ResourceCleanup);
  // Detached clients cannot callback into a finished stream.
  esp_http_client_set_user_data(handles.client,nullptr);
  const bool saved=save_http_tls_session(handles.tls);
  esp_http_client_close(handles.client);
  esp_http_client_cleanup(handles.client);
  release_http_tls_session(handles.tls,saved);
}
HttpConnectionLease detach_locked() {
  auto handles=slot.handles;slot.handles={};expires_us.store(0);
  return handles;
}
void retire() {
  HttpConnectionLease handles;
  std::shared_ptr<const NetworkTlsPolicy> trust;
  {
    StaticLockGuard lock(mutex);
    if(slot.leased||slot.retiring||!slot.handles.client)return;
    if(esp_timer_get_time()<expires_us.load() &&
        slot.policy_generation==http_tls_policy_generation() && room())return;
    slot.retiring=true;handles=detach_locked();trust=slot.trust;
  }
  destroy(handles);
  StaticLockGuard lock(mutex);slot={};
}
}
HttpConnectionLease acquire_http_connection(const NetworkRequest &request,
    std::shared_ptr<const NetworkTlsPolicy> trust) {
  // Cleanup runs here only on the requesting I/O owner. A low-memory cold
  // operation must not compete with expendable idle TLS storage.
  if(!room()){retire();return {};}
  if(!request.keep_alive || !trust || trust->mode=="skip_verify" ||
      request.url.rfind("https://",0)!=0)return {};
  // Bound the header object count as well as string bytes. Empty headers
  // otherwise evade a byte-only bound. Larger requests still run uncached.
  if(request.headers.size()>32)return {};
  size_t bytes=request.connection_scope.size();if(bytes>4096)return {};
  for(const auto &header:request.headers){bytes+=header.key.size()+header.value.size();if(bytes>4096)return {};}
  std::string origin;
  const auto end=request.url.find_first_of("/?#",8);
  if(!network_tls_normalize_origin(request.url.substr(0,end),origin))return {};
  HttpConnectionLease retired,result;
  std::shared_ptr<const NetworkTlsPolicy> old_trust;
  const auto now=esp_timer_get_time();
  {
    StaticLockGuard lock(mutex);
    if(slot.leased||slot.retiring||!next_generation)return {};
    slot.leased=true;
    const auto revision=http_tls_policy_generation();
    const bool matches=slot.trust&&slot.origin==origin&&
      slot.trust->mode==trust->mode&&slot.trust->ca_pem==trust->ca_pem&&
      slot.trust->origin==trust->origin&&slot.scope==request.connection_scope&&
      (!request.connection_scope.empty()||same_headers(slot.headers,request.headers))&&
      slot.policy_generation==revision&&now<expires_us.load();
    if(!matches){retired=detach_locked();old_trust=slot.trust;
      slot.trust=std::move(trust);slot.origin=std::move(origin);
      slot.headers=request.headers;slot.created_us=now;slot.policy_generation=revision;}
    slot.scope=request.connection_scope;slot.headers=request.headers;
    result=slot.handles;slot.handles={};expires_us.store(0);
    result.generation=slot.generation=next_generation++;
  }
  destroy(retired);
  if(result.client&&known_closed(result.client)) {
    // This check is before open()/HTTP send. A known-dead idle socket can
    // be replaced safely; a later send failure is never replayed.
    destroy(result);result.client=nullptr;result.tls={};
    StaticLockGuard lock(mutex);slot.created_us=esp_timer_get_time();
  }
  return result;
}
bool retain_http_connection(HttpConnectionLease &lease,
    esp_http_client_handle_t &client,HttpTlsSessionLease &tls) {
  if(!lease || !client)return false;
  // The SDK retains header objects; clear all caller headers before handing
  // back a client. A later request cannot inherit credentials or protocol data.
  // This owner is exclusive, so SDK calls need no cache mutex.
  for(const auto &header:slot.headers)
    if(esp_http_client_delete_header(client,header.key.c_str())!=ESP_OK)return false;
  esp_http_client_set_user_data(client,nullptr);
  const auto now=esp_timer_get_time();
  StaticLockGuard lock(mutex);
  if(!slot.leased||slot.generation!=lease.generation||
      slot.policy_generation!=http_tls_policy_generation()||
      now>=slot.created_us+kLifetimeUs||!room())return false;
  slot.handles={client,tls,lease.generation};client=nullptr;tls={};
  slot.leased=false;
  expires_us.store(std::min(now+kIdleUs,slot.created_us+kLifetimeUs));
  lease={};return true;
}
void release_http_connection(HttpConnectionLease &lease) {
  if(!lease)return;
  StaticLockGuard lock(mutex);
  if(slot.leased&&slot.generation==lease.generation)slot={};
  lease={};
}
void poll_http_connections() {
  const auto deadline=expires_us.load();if(!deadline)return;
  const auto now=esp_timer_get_time();
  static int64_t next_probe=0,retry_at=0;
  if(now<next_probe)return;
  next_probe=now+1'000'000;
  bool expired;
  {StaticLockGuard lock(mutex);expired=slot.policy_generation!=http_tls_policy_generation();}
  if(!expired&&now<deadline&&room())return;
  static NetworkJobHandle retirement;
  if(now<retry_at||network_job_active(retirement))return;
  const NetworkJobDefinition job{.owner_service_id=kNetworkRuntimeServiceId,
    .kind="http_connection_retirement",.priority=kNetworkPriorityMaintenance,
    .runner=[](NetworkJobHandle,void *){retire();return NetworkJobRunResult{true,false,{}};},
    .job_class=NetworkJobClass::Retirement};
  std::string error;if(!submit_network_job(job,retirement,error))retry_at=now+1'000'000;
}
bool http_connection_owned() {
  StaticLockGuard lock(mutex);return slot.leased||slot.retiring||slot.handles.client;
}
}
