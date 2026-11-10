#include <cassert>
#include <cstdio>
#include <string>
#include "bsp/lunaredu_bsp.h"
static bool live=false, fail_free=true;
static int free_calls=0;
esp_err_t spi_bus_initialize(spi_host_device_t,const spi_bus_config_t*,int) {
  if(live) return ESP_ERR_INVALID_STATE;
  live=true; return ESP_OK;
}
esp_err_t spi_bus_free(spi_host_device_t) {
  ++free_calls;
  if(fail_free) return ESP_ERR_INVALID_STATE;
  live=false; return ESP_OK;
}
int main() {
  bsp_resource_reset_all();
  bsp_spi_bus_t bus{"spi", 1, 41, -1, 14, 1024, BSP_RESOURCE_EXPANSION_SPI1};
  assert(bsp_spi_init(&bus)==ESP_OK);
  assert(bsp_spi_deinit()==ESP_ERR_INVALID_STATE);
  assert(live && bsp_spi_ready());
  assert(bsp_resource_is_acquired(BSP_RESOURCE_EXPANSION_SPI1));
  fail_free=false;
  assert(bsp_spi_deinit()==ESP_OK && free_calls==2 && !live && !bsp_spi_ready());
  assert(!bsp_resource_is_acquired(BSP_RESOURCE_EXPANSION_SPI1));
  const int pins[]={41,14};
  bsp_i2s_resource_claim_t first{}, second{};
  assert(bsp_i2s_claim_pins(pins,2,&first)==ESP_OK);
  auto lease=first.lease_id;
  assert(bsp_i2s_claim_pins(pins,2,&first)==ESP_ERR_INVALID_STATE && first.lease_id==lease);
  assert(bsp_i2s_claim_pins(pins,2,&second)==ESP_ERR_INVALID_STATE && second.lease_id==0);
  assert(bsp_resource_is_acquired(BSP_RESOURCE_EXPANSION_SPI1));
  assert(bsp_i2s_release_pins(&first)==ESP_OK);
  assert(bsp_i2s_claim_pins(pins,2,&second)==ESP_OK);
  auto moved=second; second={};
  assert(bsp_i2s_release_pins(&moved)==ESP_OK);
  assert(!bsp_resource_is_acquired(BSP_RESOURCE_EXPANSION_SPI1));
  std::string owner(40,'x');
  assert(bsp_resource_acquire(BSP_RESOURCE_EXPANSION_SPI1,owner.c_str())==ESP_ERR_INVALID_ARG);
  owner.resize(31);
  assert(bsp_resource_acquire(BSP_RESOURCE_EXPANSION_SPI1,owner.c_str())==ESP_OK);
  assert(bsp_resource_release(BSP_RESOURCE_EXPANSION_SPI1,owner.c_str())==ESP_OK);
  puts("PASS BSP failed release retry, exclusive leases, transfer and owner bounds");
}
