#include "network/local_access/captive_dns_server.h"

#include <string.h>
#include <atomic>
#include <cerrno>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <esp_log.h>
#include "kernel/managed_task.h"
#include "kernel/runtime.h"

namespace lunaredu::lunaros {

namespace {

constexpr uint16_t kDnsPort = 53;

std::atomic<CaptiveDnsState> g_dns_state{CaptiveDnsState::Stopped};
std::atomic<bool> g_captive_dns_running{false};
std::atomic<bool> g_captive_dns_worker_active{false};
uint32_t g_response_ip = 0;
KernelRuntime *g_runtime = nullptr;

size_t dns_question_end(const uint8_t *packet, size_t size) {
  if (packet == nullptr || size < 12) {
    return 0;
  }

  size_t cursor = 12;
  while (cursor < size) {
    const uint8_t label_size = packet[cursor++];
    if (cursor - 12 > 255) {
      return 0;
    }
    if (label_size == 0) {
      return cursor + 4 <= size ? cursor + 4 : 0;
    }
    if ((label_size & 0xC0) != 0 || cursor + label_size > size) {
      return 0;
    }
    cursor += label_size;
  }
  return 0;
}

size_t build_captive_dns_response(const uint8_t *request, size_t request_size,
                                  uint8_t *response, size_t response_size) {
  // This local responder handles one ordinary, uncompressed DNS question.
  if (request == nullptr || request_size < 12 || response == nullptr ||
      (request[2] & 0xF8) != 0 || request[4] != 0 || request[5] != 1) {
    return 0;
  }
  const size_t question_end = dns_question_end(request, request_size);
  if (question_end == 0) {
    return 0;
  }
  const uint16_t query_type = static_cast<uint16_t>(request[question_end - 4]) << 8 |
                              request[question_end - 3];
  const uint16_t query_class = static_cast<uint16_t>(request[question_end - 2]) << 8 |
                               request[question_end - 1];
  const bool internet_class = query_class == 1 || query_class == 255;
  const bool answer_address = internet_class && (query_type == 1 || query_type == 255);
  if (response_size < question_end + (answer_address ? 16 : 0)) {
    return 0;
  }

  memcpy(response, request, question_end);
  response[2] = 0x80 | (request[2] & 0x01); // Echo RD; recursion is unavailable.
  response[3] = internet_class ? 0x00 : 0x05; // Refuse unsupported classes.
  response[4] = 0x00;
  response[5] = 0x01;
  response[6] = 0x00;
  response[7] = answer_address ? 0x01 : 0x00;
  response[8] = 0x00;
  response[9] = 0x00;
  response[10] = 0x00;
  response[11] = 0x00;
  // AAAA and other record types have no local data, rather than an unrelated A RR.
  if (!answer_address) return question_end;

  size_t cursor = question_end;
  response[cursor++] = 0xC0;
  response[cursor++] = 0x0C;
  response[cursor++] = 0x00;
  response[cursor++] = 0x01;
  response[cursor++] = 0x00;
  response[cursor++] = 0x01;
  response[cursor++] = 0x00;
  response[cursor++] = 0x00;
  response[cursor++] = 0x00;
  response[cursor++] = 0x3C;
  response[cursor++] = 0x00;
  response[cursor++] = 0x04;
  response[cursor++] = static_cast<uint8_t>((g_response_ip >> 24) & 0xFF);
  response[cursor++] = static_cast<uint8_t>((g_response_ip >> 16) & 0xFF);
  response[cursor++] = static_cast<uint8_t>((g_response_ip >> 8) & 0xFF);
  response[cursor++] = static_cast<uint8_t>(g_response_ip & 0xFF);
  return cursor;
}

void captive_dns_task(void *parameter) {
  (void)parameter;

  const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (socket_fd < 0) {
    ESP_LOGW("captive_dns", "创建 DNS 套接字失败");
    g_dns_state.store(CaptiveDnsState::Failed, std::memory_order_release);
    g_captive_dns_running = false;
    g_captive_dns_worker_active.store(false, std::memory_order_release);
    return;
  }

  const int reuse = 1;
  const auto reuse_result=setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  timeval timeout = {};
  timeout.tv_sec = 0;
  timeout.tv_usec = 250000;
  if (reuse_result < 0 || setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    close(socket_fd);
    g_captive_dns_running = false;
    g_dns_state.store(CaptiveDnsState::Failed, std::memory_order_release);
    g_captive_dns_worker_active.store(false, std::memory_order_release);
    return;
  }

  sockaddr_in server_address = {};
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(kDnsPort);
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(socket_fd, reinterpret_cast<sockaddr *>(&server_address),
           sizeof(server_address)) < 0) {
    ESP_LOGW("captive_dns", "绑定 DNS 套接字失败");
    close(socket_fd);
    g_dns_state.store(CaptiveDnsState::Failed, std::memory_order_release);
    g_captive_dns_running = false;
    g_captive_dns_worker_active.store(false, std::memory_order_release);
    return;
  }

  g_dns_state.store(CaptiveDnsState::Ready, std::memory_order_release);
  ESP_LOGD("captive_dns", "Captive DNS 已启动");
  uint8_t request[512];
  uint8_t response[512];
  while (g_captive_dns_running) {
    sockaddr_in client_address = {};
    socklen_t client_address_len = sizeof(client_address);
    const int received =
        recvfrom(socket_fd, request, sizeof(request), 0,
                 reinterpret_cast<sockaddr *>(&client_address),
                 &client_address_len);
    if (!g_captive_dns_running) break;
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
      ESP_LOGW("captive_dns", "DNS 接收失败: %d", errno);
      g_dns_state.store(CaptiveDnsState::Failed, std::memory_order_release);
      g_captive_dns_running = false;
      break;
    }
    if (received == 0) {
      continue;
    }

    const size_t response_size =
        build_captive_dns_response(request, static_cast<size_t>(received),
                                   response, sizeof(response));
    if (response_size == 0) {
      continue;
    }

    sendto(socket_fd, response, response_size, 0,
           reinterpret_cast<sockaddr *>(&client_address), client_address_len);
  }

  close(socket_fd);
  ESP_LOGD("captive_dns", "Captive DNS 已停止");
  g_captive_dns_worker_active.store(false, std::memory_order_release);
}

} // namespace

bool start_captive_dns_server(KernelRuntime &runtime, uint32_t response_ip) {
  if (!g_captive_dns_worker_active.load() && g_runtime != nullptr &&
      g_runtime->task_active("captive_dns")) return false;
  bool idle = false;
  if (!g_captive_dns_worker_active.compare_exchange_strong(idle, true)) {
    return g_runtime == &runtime && g_captive_dns_running.load();
  }

  g_dns_state.store(CaptiveDnsState::Preparing, std::memory_order_release);
  g_runtime = &runtime;
  g_response_ip = response_ip;
  g_captive_dns_running = true;
  std::string error;
  const lunaredu::lunaros::ManagedTaskDefinition definition{
      .id = "captive_dns",
      .owner_service_id = "network.runtime",
      .role = "captive_dns",
      .stack_bytes = 3072,
      .priority = 1,
      .core_affinity = lunaredu::lunaros::kManagedTaskIoCore,
      .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Feature,
      .memory = lunaredu::lunaros::ManagedTaskMemory::Internal,
      .allow_internal_fallback = false,
  };
  if (!lunaredu::lunaros::create_managed_task(
          runtime, definition, captive_dns_task, nullptr, nullptr, error)) {
    g_dns_state.store(CaptiveDnsState::Failed, std::memory_order_release);
    g_captive_dns_running = false;
    g_captive_dns_worker_active.store(false, std::memory_order_release);
    g_runtime = nullptr;
    ESP_LOGW("captive_dns", "创建 Captive DNS 任务失败: %s", error.c_str());
    return false;
  }
  return true;
}

bool stop_captive_dns_server() {
  g_captive_dns_running.store(false);
  // The managed-task wrapper releases its runtime admission after the worker
  // returns. Observe that release without blocking the UI/radio owner.
  if (g_runtime != nullptr && g_runtime->task_active("captive_dns")) return false;
  // Admission can also end when activation fails before the worker is called.
  g_captive_dns_worker_active.store(false, std::memory_order_release);
  g_runtime = nullptr;
  g_dns_state.store(CaptiveDnsState::Stopped, std::memory_order_release);
  return true;
}

CaptiveDnsState captive_dns_server_state() {
  const auto state = g_dns_state.load(std::memory_order_acquire);
  if (state == CaptiveDnsState::Preparing && g_runtime && !g_runtime->task_active("captive_dns"))
    return CaptiveDnsState::Failed;
  return state;
}

bool captive_dns_server_running() { return g_captive_dns_worker_active.load(std::memory_order_acquire); }

} // namespace lunaredu::lunaros
