#include "bsp/boards/general/v2.h"
#include <driver/gpio.h>
#include <mutex>
namespace {std::mutex mutex;bool ready=false,claimed=false;constexpr const char *owner="v2.spi";}
esp_err_t bsp_v2_spi_init() {
  std::lock_guard<std::mutex> lock(mutex);
  const auto *b=bsp_v2_board();if(!b || !b->spi_available)return ESP_ERR_NOT_SUPPORTED;
  if(ready)return ESP_OK;
  if(claimed)return ESP_ERR_INVALID_STATE;
  auto e=bsp_v2_resource_acquire(BSP_V2_RESOURCE_SPI_BUS,owner);if(e!=ESP_OK)return e;
  claimed=true;
  const bool screen=b->screen.mode!=bsp_v2_display_t::none;
  bool guarded_screen=false,guarded_card=false;
  if(screen) {e=bsp_v2_resource_acquire(BSP_V2_RESOURCE_SCREEN,owner);guarded_screen=e==ESP_OK;}
  if(e==ESP_OK && b->tf_enabled) {e=bsp_v2_resource_acquire(BSP_V2_RESOURCE_TFCARD,owner);guarded_card=e==ESP_OK;}
  const auto high=[&](int pin) {
    auto r=gpio_set_level(static_cast<gpio_num_t>(pin),1);if(r!=ESP_OK)return r;
    gpio_config_t c{};c.pin_bit_mask=uint64_t{1}<<pin;c.mode=GPIO_MODE_OUTPUT;c.intr_type=GPIO_INTR_DISABLE;
    return gpio_config(&c);
  };
  if(e==ESP_OK && screen)e=high(b->screen.cs);
  if(e==ESP_OK && b->tf_enabled)e=high(b->tf_cs);
  if(e==ESP_OK) {
    spi_bus_config_t c{};c.mosi_io_num=b->spi.mosi_pin;c.miso_io_num=b->spi.miso_pin;c.sclk_io_num=b->spi.sclk_pin;
    const bool quad=b->screen.mode==bsp_v2_display_t::amoled_qspi;
    c.quadwp_io_num=quad?b->screen.data2:GPIO_NUM_NC;c.quadhd_io_num=quad?b->screen.data3:GPIO_NUM_NC;
    c.max_transfer_sz=b->spi.max_transfer_sz;
    e=spi_bus_initialize(b->spi.host,&c,SPI_DMA_CH_AUTO);ready=e==ESP_OK;
  }
  if(guarded_card) {const auto r=bsp_v2_resource_release(BSP_V2_RESOURCE_TFCARD,owner);if(e==ESP_OK)e=r;}
  if(guarded_screen) {const auto r=bsp_v2_resource_release(BSP_V2_RESOURCE_SCREEN,owner);if(e==ESP_OK)e=r;}
  if(!ready && bsp_v2_resource_release(BSP_V2_RESOURCE_SPI_BUS,owner)==ESP_OK)claimed=false;
  return e;
}
esp_err_t bsp_v2_spi_deinit() {
  std::lock_guard<std::mutex> lock(mutex);
  const auto *b=bsp_v2_board();if(!b)return ESP_ERR_NOT_SUPPORTED;
  if(ready) {const auto e=spi_bus_free(b->spi.host);if(e!=ESP_OK)return e;ready=false;}
  if(claimed) {const auto e=bsp_v2_resource_release(BSP_V2_RESOURCE_SPI_BUS,owner);if(e!=ESP_OK)return e;claimed=false;}
  return ESP_OK;
}
bool bsp_v2_spi_ready() {std::lock_guard<std::mutex> lock(mutex);return ready;}
