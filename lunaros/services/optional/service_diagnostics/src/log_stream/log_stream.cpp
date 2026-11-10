#include "log_stream_transport.h"
#include "../diagnostic_log_output.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include <esp_attr.h>
#endif

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <lwip/errno.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>


namespace lunaredu::lunaros::log_stream {
namespace {

constexpr size_t kRingBytes = 16 * 1024;
constexpr size_t kChunkBytes = 1024;
constexpr size_t kMaxDrainBytesPerTick = 2 * kChunkBytes;
// Normal SDK vprintf hook / task-owned TCP history only, never DMA or an
// early/DRAM/ISR log sink. Keep the full boot history out of scarce internal
// SRAM on PSRAM targets; non-PSRAM targets keep their existing static storage.
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
std::array<char, kRingBytes> g_ring{};
portMUX_TYPE g_ring_mux = portMUX_INITIALIZER_UNLOCKED;
uint64_t g_total_bytes = 0;
int g_server_fd = -1;
int g_client_fd = -1;
uint64_t g_client_cursor = 0;
bool g_initialized = false;
Config g_config;

uint64_t oldest_byte() {
  return g_total_bytes > kRingBytes ? g_total_bytes - kRingBytes : 0;
}

void append_ring(const char *data, size_t length) {
  if (data == nullptr || length == 0) return;
  if (length > kRingBytes) {
    data += length - kRingBytes;
    length = kRingBytes;
  }
  portENTER_CRITICAL(&g_ring_mux);
  const auto offset=size_t(g_total_bytes%kRingBytes);
  const auto first=std::min(length,kRingBytes-offset);
  std::memcpy(g_ring.data()+offset,data,first);
  if(first<length)std::memcpy(g_ring.data(),data+first,length-first);
  g_total_bytes += length;
  portEXIT_CRITICAL(&g_ring_mux);
}

void log_sink(const char *format, va_list arguments) {
  char line[768];
  va_list capture_arguments;
  va_copy(capture_arguments, arguments);
  const int written = std::vsnprintf(line, sizeof(line), format, capture_arguments);
  va_end(capture_arguments);
  if (written <= 0) return;
  size_t length = static_cast<size_t>(written);
  if (length >= sizeof(line)) length = sizeof(line) - 1;
  append_ring(line, length);
}

void close_client() {
  if (g_client_fd >= 0) {
    lwip_close(g_client_fd);
    g_client_fd = -1;
  }
}

bool set_nonblocking(int fd) {
  const int flags = lwip_fcntl(fd, F_GETFL, 0);
  return flags >= 0 && lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool start_server() {
  if (g_server_fd >= 0) return true;
  g_server_fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (g_server_fd < 0) return false;
  const int reuse = 1;
  (void)setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(g_config.port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (lwip_bind(g_server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      lwip_listen(g_server_fd, 1) != 0 || !set_nonblocking(g_server_fd)) {
    lwip_close(g_server_fd);
    g_server_fd = -1;
    return false;
  }
  return true;
}

void accept_client() {
  if (g_server_fd < 0) return;
  sockaddr_in address = {};
  socklen_t length = sizeof(address);
  const int client = lwip_accept(g_server_fd, reinterpret_cast<sockaddr *>(&address), &length);
  if (client < 0) return;
  if (!set_nonblocking(client)) { lwip_close(client); return; }
  close_client();
  g_client_fd = client;
  portENTER_CRITICAL(&g_ring_mux);
  g_client_cursor = oldest_byte();
  portEXIT_CRITICAL(&g_ring_mux);
  (void)lwip_send(g_client_fd, g_config.greeting.data(), g_config.greeting.size(), MSG_DONTWAIT);
}

void drain_client() {
  if (g_client_fd < 0) return;
  char discard[32];
  const int received = lwip_recv(g_client_fd, discard, sizeof(discard), MSG_DONTWAIT);
  if (received == 0 || (received < 0 && errno != EWOULDBLOCK && errno != EAGAIN)) {
    close_client();
    return;
  }
  size_t budget = kMaxDrainBytesPerTick;
  while (budget != 0) {
    char chunk[kChunkBytes];
    size_t length = 0;
    portENTER_CRITICAL(&g_ring_mux);
    const uint64_t oldest = oldest_byte();
    if (g_client_cursor < oldest) g_client_cursor = oldest;
    const uint64_t available = g_total_bytes - g_client_cursor;
    if (available != 0) {
      length = static_cast<size_t>(std::min<uint64_t>(available,std::min(sizeof(chunk),budget)));
      const auto offset=size_t(g_client_cursor%kRingBytes);
      const auto first=std::min(length,kRingBytes-offset);
      std::memcpy(chunk,g_ring.data()+offset,first);
      if(first<length)std::memcpy(chunk+first,g_ring.data(),length-first);
    }
    portEXIT_CRITICAL(&g_ring_mux);
    if (length == 0) break;
    const int sent = lwip_send(g_client_fd, chunk, length, MSG_DONTWAIT);
    if (sent <= 0) {
      if (errno != EWOULDBLOCK && errno != EAGAIN) close_client();
      break;
    }
    g_client_cursor += static_cast<uint64_t>(sent);
    budget -= static_cast<size_t>(sent);
    if (static_cast<size_t>(sent) < length) break;
  }
}

}  // namespace

void initialize(const Config &config) {
  if (g_initialized) return;
  g_config = config;
  g_initialized = true;
  internal::capture::attach_diagnostic_log_output(log_sink);
}

void tick() {
  if (!g_initialized) return;
  if (!start_server()) return;
  accept_client();
  drain_client();
}

void shutdown() {
  close_client();
  if (g_server_fd >= 0) {
    lwip_close(g_server_fd);
    g_server_fd = -1;
  }
  if (g_initialized) {
    internal::capture::detach_diagnostic_log_output(log_sink);
    g_initialized = false;
  }
}

bool client_connected() { return g_client_fd >= 0; }

}  // namespace lunaredu::lunaros::log_stream
