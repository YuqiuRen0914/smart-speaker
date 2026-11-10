#include "network/provisioning/captive_dns_server.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/kernel/managed_task.h"

namespace lunaredu::lunaros {

namespace {

constexpr uint16_t kDnsPort = 53;

volatile bool g_captive_dns_running = false;
TaskHandle_t g_captive_dns_task = nullptr;
uint32_t g_response_ip = 0;

size_t dns_question_end(const uint8_t *packet, size_t size) {
  if (packet == nullptr || size < 12) {
    return 0;
  }

  size_t cursor = 12;
  while (cursor < size) {
    const uint8_t label_size = packet[cursor++];
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
  const size_t question_end = dns_question_end(request, request_size);
  if (question_end == 0 || response_size < question_end + 16) {
    return 0;
  }

  memcpy(response, request, question_end);
  response[2] = 0x81;
  response[3] = 0x80;
  response[4] = 0x00;
  response[5] = 0x01;
  response[6] = 0x00;
  response[7] = 0x01;
  response[8] = 0x00;
  response[9] = 0x00;
  response[10] = 0x00;
  response[11] = 0x00;

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
    LOGW("captive_dns", "创建 DNS 套接字失败");
    g_captive_dns_running = false;
    g_captive_dns_task = nullptr;
    return;
  }

  const int reuse = 1;
  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  timeval timeout = {};
  timeout.tv_sec = 0;
  timeout.tv_usec = 250000;
  setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  sockaddr_in server_address = {};
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(kDnsPort);
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(socket_fd, reinterpret_cast<sockaddr *>(&server_address),
           sizeof(server_address)) < 0) {
    LOGW("captive_dns", "绑定 DNS 套接字失败");
    close(socket_fd);
    g_captive_dns_running = false;
    g_captive_dns_task = nullptr;
    return;
  }

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
    if (received <= 0) {
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
  g_captive_dns_task = nullptr;
}

} // namespace

void start_captive_dns_server(uint32_t response_ip) {
  if (g_captive_dns_task != nullptr) {
    return;
  }

  g_response_ip = response_ip;
  g_captive_dns_running = true;
  std::string error;
  const lunaredu::lunaros::ManagedTaskDefinition definition{
      .id = "captive_dns",
      .owner_service_id = "network.provisioning",
      .role = "captive_dns",
      .stack_bytes = 3072,
      .priority = 1,
      .core_affinity = -1,
      .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Feature,
      .memory = lunaredu::lunaros::ManagedTaskMemory::Internal,
      .allow_internal_fallback = false,
  };
  if (!lunaredu::lunaros::create_managed_task(
          definition, captive_dns_task, nullptr, &g_captive_dns_task, error)) {
    g_captive_dns_running = false;
    g_captive_dns_task = nullptr;
    LOGW("captive_dns", "创建 Captive DNS 任务失败: %s", error.c_str());
  }
}

void stop_captive_dns_server() { g_captive_dns_running = false; }

bool captive_dns_server_running() { return g_captive_dns_task != nullptr; }

} // namespace lunaredu::lunaros
