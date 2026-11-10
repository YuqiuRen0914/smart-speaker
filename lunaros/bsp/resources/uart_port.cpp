#include "bsp/uart_port.h"
#include <mutex>

namespace lunaredu::bsp {
namespace {
std::mutex g_mutex;
const void *g_owners[UART_NUM_MAX] = {};
bool valid_port(uart_port_t port) {
  return static_cast<int>(port) >= 0 && port < UART_NUM_MAX;
}
}
esp_err_t acquire_uart_port(uart_port_t port, const void *owner,
                            const uart_config_t &config, int tx_pin,
                            int rx_pin, int rx_buffer_size) {
  if (!valid_port(port) || owner == nullptr) return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_owners[port] != nullptr || uart_is_driver_installed(port)) {
    return ESP_ERR_INVALID_STATE;
  }
  // Installing first reserves the IDF driver before changing hardware. All
  // Device UART users serialize this transaction through the same mutex.
  esp_err_t result = uart_driver_install(port, rx_buffer_size, 0, 0, nullptr, 0);
  if (result != ESP_OK) return result;
  g_owners[port] = owner;
  result = uart_param_config(port, &config);
  if (result == ESP_OK) {
    result = uart_set_pin(port, tx_pin, rx_pin,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
  if (result != ESP_OK) {
    if (uart_driver_delete(port) == ESP_OK) g_owners[port] = nullptr;
    return result;
  }
  return ESP_OK;
}
esp_err_t release_uart_port(uart_port_t port, const void *owner) {
  if (!valid_port(port) || owner == nullptr) return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_owners[port] == nullptr) return ESP_OK;
  if (g_owners[port] != owner) return ESP_ERR_INVALID_STATE;
  const esp_err_t result = uart_driver_delete(port);
  if (result == ESP_OK) g_owners[port] = nullptr;
  return result;
}
} // namespace lunaredu::bsp
