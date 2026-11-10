#include "bsp/lunaredu_bsp.h"

#include <mutex>

namespace {

constexpr const char *kSpiOwner = "bsp_spi";

bsp_spi_bus_t g_active_bus = {};
bool g_spi_ready = false;
bool g_spi_claimed = false;
std::recursive_mutex g_spi_mutex;

bool valid_gpio_or_unused(int pin) {
  return pin == -1 || (pin >= 0 && pin < GPIO_NUM_MAX);
}

bool same_bus(const bsp_spi_bus_t &lhs, const bsp_spi_bus_t &rhs) {
  return lhs.host == rhs.host && lhs.mosi_pin == rhs.mosi_pin &&
         lhs.miso_pin == rhs.miso_pin && lhs.sclk_pin == rhs.sclk_pin &&
         lhs.max_transfer_sz == rhs.max_transfer_sz &&
         lhs.resource == rhs.resource;
}

bool resource_has_pin_if_used(bsp_resource_t resource, int pin) {
  return pin < 0 || bsp_resource_contains_pin(resource, pin);
}

esp_err_t validate_bus(const bsp_spi_bus_t *bus) {
  if (bus == nullptr || bus->id == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!valid_gpio_or_unused(bus->mosi_pin) ||
      !valid_gpio_or_unused(bus->miso_pin) ||
      !valid_gpio_or_unused(bus->sclk_pin) || bus->mosi_pin < 0 ||
      bus->sclk_pin < 0 || bus->max_transfer_sz <= 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!resource_has_pin_if_used(bus->resource, bus->mosi_pin) ||
      !resource_has_pin_if_used(bus->resource, bus->miso_pin) ||
      !resource_has_pin_if_used(bus->resource, bus->sclk_pin)) {
    return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}

} // namespace

esp_err_t bsp_spi_init(const bsp_spi_bus_t *bus) {
  std::lock_guard<std::recursive_mutex> lock(g_spi_mutex);

  const esp_err_t validation = validate_bus(bus);
  if (validation != ESP_OK) {
    return validation;
  }

  if (g_spi_ready) {
    return same_bus(g_active_bus, *bus) ? ESP_OK : ESP_ERR_INVALID_STATE;
  }

  if (g_spi_claimed) return ESP_ERR_INVALID_STATE;

  const esp_err_t claim = bsp_resource_acquire(bus->resource, kSpiOwner);
  if (claim != ESP_OK) {
    return claim;
  }

  g_active_bus = *bus;
  g_spi_claimed = true;
  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = bus->mosi_pin;
  bus_config.miso_io_num = bus->miso_pin;
  bus_config.sclk_io_num = bus->sclk_pin;
  bus_config.quadwp_io_num = GPIO_NUM_NC;
  bus_config.quadhd_io_num = GPIO_NUM_NC;
  bus_config.max_transfer_sz = bus->max_transfer_sz;

  const esp_err_t result =
      spi_bus_initialize(bus->host, &bus_config, SPI_DMA_CH_AUTO);
  if (result != ESP_OK) {
    if (bsp_resource_release(bus->resource, kSpiOwner) == ESP_OK) {
      g_spi_claimed = false;
      g_active_bus = {};
    }
    return result;
  }

  g_active_bus = *bus;
  g_spi_ready = true;
  return ESP_OK;
}

esp_err_t bsp_spi_deinit() {
  std::lock_guard<std::recursive_mutex> lock(g_spi_mutex);

  if (g_spi_ready) {
    const esp_err_t result = spi_bus_free(g_active_bus.host);
    if (result != ESP_OK) return result;
    g_spi_ready = false;
  }
  if (g_spi_claimed) {
    const esp_err_t result = bsp_resource_release(g_active_bus.resource, kSpiOwner);
    if (result != ESP_OK) return result;
    g_spi_claimed = false;
  }
  g_active_bus = {};
  return ESP_OK;
}

bool bsp_spi_ready() {
  std::lock_guard<std::recursive_mutex> lock(g_spi_mutex);
  return g_spi_ready;
}
