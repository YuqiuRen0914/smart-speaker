#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_BT_BLUEDROID_ENABLED
#include "bluetooth_registry.h"
#include "kernel/memory_admission.h"
#include <esp_bt.h>
#include <soc/soc_caps.h>
#include <esp_bt_main.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::lunaros {
namespace {
using namespace bluetooth_internal;
bool controller_initialized=false,controller_enabled=false,host_initialized=false,host_enabled=false;
bool profiles_installed=false;
bool checked(esp_err_t result,std::string &error) {
  if(result==ESP_OK)return true;
  error=esp_err_to_name(result);return false;
}
bool release_host(std::string &error) {
  // Retain each completed stage if a later teardown fails; a retry resumes
  // from that stage instead of losing ownership or initializing twice.
  // Also retire profiles on partial-install rollback. Their async SDK cleanup
  // must finish while the host and callback threads are still available.
  if(host_enabled && profiles_installed)
    for(size_t i=0;i<count;++i)
      if(profiles[i].prepare_stop&&!profiles[i].prepare_stop(profiles[i].context,error))return false;
  if(host_enabled) { if(!checked(esp_bluedroid_disable(),error))return false;host_enabled=false;running=false;synchronized=false; }
  if(host_initialized) { if(!checked(esp_bluedroid_deinit(),error))return false;host_initialized=false; }
  if(controller_enabled) { if(!checked(esp_bt_controller_disable(),error))return false;controller_enabled=false; }
  if(controller_initialized) { if(!checked(esp_bt_controller_deinit(),error))return false;controller_initialized=false; }
  if(profiles_installed) {
    for(size_t i=0;i<count;++i)if(profiles[i].reset)profiles[i].reset(profiles[i].context);
    for(size_t i=0;i<count;++i)if(profiles[i].stopped)profiles[i].stopped(profiles[i].context);
    profiles_installed=false;
  }
  return true;
}
}
bool start_bluetooth(std::string &error) {
  using namespace bluetooth_internal;
  if(running.load()){error.clear();return true;}
  if(!release_host(error))return false;
  MemoryStartWindow memory_window;MemoryStartRequirements memory;
#if CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_FREE
  memory.dma_free=CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_FREE;
#endif
#if CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_BLOCK
  memory.dma_largest=CONFIG_LUNAREDU_NETWORK_BT_START_MIN_INTERNAL_BLOCK;
#endif
  const auto waiting_since=xTaskGetTickCount();
  while(!memory_start_gate().acquire(memory,memory_window,error)) {
    // SDK task retirement and concurrent network work can temporarily split
    // the DMA heap. Let those owners finish before admitting a new controller;
    // persistent pressure still fails, with the same total/contiguous limits.
    if(xTaskGetTickCount()-waiting_since>=pdMS_TO_TICKS(2000)) {
      if(error!="memory_start_busy")error="bluetooth_internal_memory_low";
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20)>0?pdMS_TO_TICKS(20):1);
  }
  // IDF 6.1's S31 default macro omits fields whose specified default is zero.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  esp_bt_controller_config_t configuration=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#pragma GCC diagnostic pop
  esp_err_t result=esp_bt_controller_init(&configuration);
  if(result==ESP_OK){
    controller_initialized=true;
#if CONFIG_BT_CLASSIC_ENABLED && SOC_BT_CLASSIC_SUPPORTED
#if CONFIG_BT_BLE_ENABLED
    result=esp_bt_controller_enable(ESP_BT_MODE_BTDM);
#else
    result=esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
#endif
#else
    result=esp_bt_controller_enable(ESP_BT_MODE_BLE);
#endif
  }
  if(result==ESP_OK){controller_enabled=true;result=esp_bluedroid_init();}
  if(result==ESP_OK){host_initialized=true;result=esp_bluedroid_enable();}
  if(result!=ESP_OK){error=esp_err_to_name(result);std::string cleanup;(void)release_host(cleanup);return false;}
  host_enabled=true;
  profiles_installed=true;
  for(size_t i=0;i<count;++i)if(!profiles[i].install(profiles[i].context,error)) {
    std::string cleanup;(void)release_host(cleanup);return false;
  }
  sealed=true;running=true;synchronized=true;
  for(size_t i=0;i<count;++i)if(profiles[i].synchronized)profiles[i].synchronized(profiles[i].context);
  error.clear();return true;
}
bool bluetooth_occupied() {
  return controller_initialized || controller_enabled || host_initialized || host_enabled;
}
bool stop_bluetooth(std::string &error) {
  using namespace bluetooth_internal;
  if(!release_host(error))return false;
  running=false;synchronized=false;
  error.clear();return true;
}
} // namespace lunaredu::lunaros
#endif
