#pragma once
#include <driver/uart.h>

namespace lunaredu::bsp {
// owner must have a stable, unique address until release. An occupied port is
// rejected without touching its configuration, pins or buffered data.
esp_err_t acquire_uart_port(uart_port_t port, const void *owner,
                            const uart_config_t &config, int tx_pin,
                            int rx_pin, int rx_buffer_size);
esp_err_t release_uart_port(uart_port_t port, const void *owner);
} // namespace lunaredu::bsp
