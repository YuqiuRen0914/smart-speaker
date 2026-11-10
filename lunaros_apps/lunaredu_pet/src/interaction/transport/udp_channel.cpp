#include "interaction/transport/network_udp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <netdb.h>
#include <sdkconfig.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <mutex>

#include "lunaredu_lunaros/kernel/managed_task.h"

namespace {

constexpr EventBits_t kUdpReceiveTaskExit = BIT0;
constexpr const char *kTag = "network_udp";
constexpr uint32_t kUdpLogLimit = 5;
constexpr uint32_t kUdpReceiveTaskStackSize = 3072;
constexpr UBaseType_t kUdpReceiveTaskPriority = 2;
constexpr UBaseType_t kUdpReceiveTaskInternalCaps =
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
constexpr bool kUdpReceiveTaskPreferExternalStack = true;
constexpr UBaseType_t kUdpReceiveTaskCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
constexpr bool kUdpReceiveTaskPreferExternalStack = false;
constexpr UBaseType_t kUdpReceiveTaskCaps = kUdpReceiveTaskInternalCaps;
#endif

void log_udp_heap(const char *phase, BaseType_t result, bool error,
                  UBaseType_t caps) {
  const unsigned heap_free = static_cast<unsigned>(esp_get_free_heap_size());
  const unsigned heap_min =
      static_cast<unsigned>(esp_get_minimum_free_heap_size());
  const unsigned sram_free =
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  const unsigned sram_largest = static_cast<unsigned>(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  const unsigned psram_free =
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  const unsigned psram_largest = static_cast<unsigned>(
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  if (error) {
    ESP_LOGE(kTag,
             "%s result=%d stack=%" PRIu32
             " caps=0x%x heap_free=%u heap_min=%u sram_free=%u"
             " sram_largest=%u psram_free=%u psram_largest=%u",
             phase, static_cast<int>(result), kUdpReceiveTaskStackSize,
             static_cast<unsigned>(caps), heap_free, heap_min,
             sram_free, sram_largest, psram_free, psram_largest);
  } else {
    ESP_LOGI(kTag,
             "%s result=%d stack=%" PRIu32
             " caps=0x%x heap_free=%u heap_min=%u sram_free=%u"
             " sram_largest=%u psram_free=%u psram_largest=%u",
             phase, static_cast<int>(result), kUdpReceiveTaskStackSize,
             static_cast<unsigned>(caps), heap_free, heap_min,
             sram_free, sram_largest, psram_free, psram_largest);
  }
}

class NetworkOwnedUdpChannel final : public NetworkUdpChannel {
 public:
  NetworkOwnedUdpChannel() { event_group_ = xEventGroupCreate(); }

  ~NetworkOwnedUdpChannel() override {
    Disconnect();
    if (event_group_ != nullptr) {
      vEventGroupDelete(event_group_);
      event_group_ = nullptr;
    }
  }

  bool Connect(const std::string &host, int port) override {
    if (event_group_ == nullptr) {
      last_error_ = ENOMEM;
      log_udp_heap("udp event group unavailable", pdFAIL, true,
                   kUdpReceiveTaskCaps);
      return false;
    }

    bool socket_open = false;
    {
      std::lock_guard<std::mutex> socket_lock(socket_mutex_);
      socket_open = udp_fd_ != -1;
    }
    if (connected_.load(std::memory_order_acquire) || socket_open) {
      Disconnect();
    }

    sockaddr_in remote_addr = {};
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(port);

    hostent *server = gethostbyname(host.c_str());
    if (server == nullptr) {
      last_error_ = h_errno;
      ESP_LOGE(kTag, "Failed to resolve host");
      return false;
    }
    std::memcpy(&remote_addr.sin_addr, server->h_addr, server->h_length);

    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
      last_error_ = errno;
      ESP_LOGE(kTag, "Failed to create socket");
      return false;
    }

    timeval recv_timeout = {};
    recv_timeout.tv_sec = 0;
    recv_timeout.tv_usec = 250000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout,
                   sizeof(recv_timeout)) < 0) {
      last_error_ = errno;
      ESP_LOGW(kTag, "Failed to set receive timeout: errno=%d", errno);
    }

    sockaddr_in local_addr = {};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr *>(&local_addr),
             sizeof(local_addr)) < 0) {
      last_error_ = errno;
      ESP_LOGE(kTag, "Failed to bind local UDP socket: errno=%d", errno);
      close(fd);
      return false;
    }

    sockaddr_in bound_addr = {};
    socklen_t bound_len = sizeof(bound_addr);
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&bound_addr),
                    &bound_len) < 0) {
      ESP_LOGW(kTag, "Failed to read local UDP port: errno=%d", errno);
    }

    {
      std::lock_guard<std::mutex> socket_lock(socket_mutex_);
      udp_fd_ = fd;
      remote_addr_ = remote_addr;
      remote_addr_valid_ = true;
      send_logs_ = 0;
      connected_.store(true, std::memory_order_release);
    }
    receive_logs_ = 0;
    xEventGroupClearBits(event_group_, kUdpReceiveTaskExit);
    log_udp_heap("udp receive task create begin", pdPASS, false,
                 kUdpReceiveTaskCaps);

    std::string task_error;
    const lunaredu::lunaros::ManagedTaskDefinition definition{
        .id = "udp_receive",
        .owner_service_id = "assistant.xiaozhi",
        .role = "network_receive",
    .stack_bytes = kUdpReceiveTaskStackSize,
        .priority = kUdpReceiveTaskPriority,
        .core_affinity = -1,
        .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Feature,
        .memory = kUdpReceiveTaskPreferExternalStack
                      ? lunaredu::lunaros::ManagedTaskMemory::External
                      : lunaredu::lunaros::ManagedTaskMemory::Internal,
        .allow_internal_fallback = kUdpReceiveTaskPreferExternalStack,
    };
    if (!lunaredu::lunaros::create_managed_task(
            definition, ReceiveTaskEntry, this, &receive_task_handle_,
            task_error)) {
      connected_.store(false, std::memory_order_release);
      {
        std::lock_guard<std::mutex> socket_lock(socket_mutex_);
        remote_addr_valid_ = false;
        if (udp_fd_ != -1) {
          close(udp_fd_);
          udp_fd_ = -1;
        }
      }
      receive_task_handle_ = nullptr;
      last_error_ = ENOMEM;
      log_udp_heap("Failed to create UDP receive task", pdFAIL, true,
                   kUdpReceiveTaskInternalCaps);
      ESP_LOGE(kTag, "Managed task rejected: %s", task_error.c_str());
      return false;
    }
    log_udp_heap(kUdpReceiveTaskPreferExternalStack
                     ? "udp receive managed task create ok (external preferred)"
                     : "udp receive managed task create ok (internal)",
                 pdPASS, false, kUdpReceiveTaskCaps);
    ESP_LOGI(kTag, "UDP channel ready remote=%s:%d local_port=%u",
             host.c_str(), port, ntohs(bound_addr.sin_port));
    return true;
  }

  void Disconnect() override {
    connected_.store(false, std::memory_order_release);
    {
      // Serialize close with Send(). Rechecking connected under this lock
      // prevents sendto() from using a descriptor after close or after the OS
      // has recycled the descriptor number for another socket.
      std::lock_guard<std::mutex> socket_lock(socket_mutex_);
      remote_addr_valid_ = false;
      const int fd = udp_fd_;
      udp_fd_ = -1;
      if (fd != -1) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
      }
    }

    if (receive_task_handle_ != nullptr) {
      auto bits = xEventGroupWaitBits(event_group_, kUdpReceiveTaskExit,
                                      pdFALSE, pdFALSE,
                                      pdMS_TO_TICKS(3000));
      if ((bits & kUdpReceiveTaskExit) == 0) {
        // The channel owns both the task argument (`this`) and the event
        // group used by ReceiveTaskEntry. Returning here would let the
        // destructor free both while the receive task can still access them.
        // A closed socket has a bounded receive timeout, so this path should
        // only be reached under severe scheduler/network-stack pressure. Keep
        // waiting fail-closed instead of turning a transport fault into a
        // use-after-free and a seemingly unrelated device restart.
        ESP_LOGE(kTag,
                 "UDP receive task missed shutdown deadline; waiting for safe exit");
        bits = xEventGroupWaitBits(event_group_, kUdpReceiveTaskExit,
                                   pdFALSE, pdFALSE, portMAX_DELAY);
      }
      (void)bits;
      receive_task_handle_ = nullptr;

      // ReceiveTaskEntry signals just before returning to the managed-task
      // wrapper. Give that wrapper time to release its registry admission and
      // delete the FreeRTOS task before a fast reconnect tries to create the
      // next task with the same managed id.
      while (xTaskGetHandle("udp_receive") != nullptr) {
        vTaskDelay(1);
      }
    }
  }

  int Send(const std::string &data) override {
    std::lock_guard<std::mutex> socket_lock(socket_mutex_);
    if (!connected_.load(std::memory_order_acquire) ||
        !remote_addr_valid_ || udp_fd_ == -1) {
      ESP_LOGE(kTag, "Not connected");
      return -1;
    }

    const int ret =
        sendto(udp_fd_, data.data(), data.size(), 0,
               reinterpret_cast<sockaddr *>(&remote_addr_),
               sizeof(remote_addr_));
    if (ret <= 0) {
      ESP_LOGE(kTag, "Send failed: ret=%d, errno=%d", ret, errno);
    } else if (send_logs_ < kUdpLogLimit) {
      send_logs_ += 1;
      ESP_LOGI(kTag, "UDP send bytes=%d count=%" PRIu32, ret, send_logs_);
    }
    return ret;
  }

  void OnMessage(
      std::function<void(const std::string &data)> callback) override {
    message_callback_ = std::move(callback);
  }

  bool IsConnected() const override { return connected_.load(); }

  int GetLastError() override {
    return last_error_.load(std::memory_order_acquire);
  }

 private:
  static void ReceiveTaskEntry(void *arg) {
    auto *self = static_cast<NetworkOwnedUdpChannel *>(arg);
    self->ReceiveTask();
    if (self->event_group_ != nullptr) {
      xEventGroupSetBits(self->event_group_, kUdpReceiveTaskExit);
    }
  }

  void ReceiveTask() {
    int fd = -1;
    {
      std::lock_guard<std::mutex> socket_lock(socket_mutex_);
      fd = udp_fd_;
    }
    std::string data;
    while (connected_.load(std::memory_order_acquire) && fd != -1) {
      data.resize(1500);
      sockaddr_in source_addr = {};
      socklen_t source_len = sizeof(source_addr);
      const int ret =
          recvfrom(fd, data.data(), data.size(), 0,
                   reinterpret_cast<sockaddr *>(&source_addr), &source_len);
      if (ret <= 0) {
        if (!connected_.load(std::memory_order_acquire)) {
          break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }
        last_error_ = errno;
        connected_.store(false, std::memory_order_release);
        {
          std::lock_guard<std::mutex> socket_lock(socket_mutex_);
          remote_addr_valid_ = false;
        }
        break;
      }

      if (receive_logs_ < kUdpLogLimit) {
        receive_logs_ += 1;
        ESP_LOGI(kTag, "UDP receive bytes=%d count=%" PRIu32
                       " source=%s:%u",
                 ret, receive_logs_, inet_ntoa(source_addr.sin_addr),
                 ntohs(source_addr.sin_port));
      }

      if (message_callback_) {
        data.resize(ret);
        message_callback_(data);
      }
    }
  }

  int udp_fd_ = -1;
  sockaddr_in remote_addr_ = {};
  std::mutex socket_mutex_;
  EventGroupHandle_t event_group_ = nullptr;
  TaskHandle_t receive_task_handle_ = nullptr;
  std::function<void(const std::string &data)> message_callback_;
  std::atomic<bool> connected_{false};
  bool remote_addr_valid_ = false;
  uint32_t send_logs_ = 0;
  uint32_t receive_logs_ = 0;
  std::atomic<int> last_error_{0};
};

} // namespace

std::unique_ptr<NetworkUdpChannel> network_open_udp_channel() {
  return std::make_unique<NetworkOwnedUdpChannel>();
}
