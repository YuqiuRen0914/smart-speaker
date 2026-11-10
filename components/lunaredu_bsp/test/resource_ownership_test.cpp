#include "bsp/uart_port.h"
#include "bsp/analog_adc_unit_pool.h"
#include <cassert>
#include <cstdio>
extern "C" void test_reset_actuator_stubs();
extern "C" int test_uart_config_writes();
extern "C" int test_uart_baud(int);
extern "C" int test_uart_rx_pin(int);
extern "C" void test_uart_fail_stage(int);
namespace {
int handles[2]; bool allocated[2] = {}; int creates = 0, deletes = 0; bool fail_adc = false;
}
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *c, adc_oneshot_unit_handle_t *h) {
  if (fail_adc || allocated[c->unit_id]) return ESP_FAIL;
  allocated[c->unit_id] = true; *h = &handles[c->unit_id]; ++creates; return ESP_OK;
}
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t h) {
  for (int i=0;i<2;++i) if(h == &handles[i] && allocated[i]) {
    allocated[i]=false; ++deletes; return ESP_OK;
  }
  return ESP_ERR_INVALID_ARG;
}
int main() {
  using namespace lunaredu::bsp;
  using namespace lunaredu::device;
  test_reset_actuator_stubs();
  int owner_a=0, owner_b=0;
  uart_config_t first{}; first.baud_rate=4800;
  uart_config_t second{}; second.baud_rate=115200;
  assert(acquire_uart_port(UART_NUM_1,&owner_a,first,-1,5,256)==ESP_OK);
  const int writes=test_uart_config_writes();
  assert(acquire_uart_port(UART_NUM_1,&owner_b,second,7,8,256)==ESP_ERR_INVALID_STATE);
  assert(test_uart_config_writes()==writes && test_uart_baud(1)==4800 && test_uart_rx_pin(1)==5);
  assert(release_uart_port(UART_NUM_1,&owner_b)==ESP_ERR_INVALID_STATE);
  assert(uart_is_driver_installed(UART_NUM_1));
  assert(release_uart_port(UART_NUM_1,&owner_a)==ESP_OK);
  assert(acquire_uart_port(UART_NUM_1,&owner_b,second,7,8,256)==ESP_OK);
  assert(release_uart_port(UART_NUM_1,&owner_b)==ESP_OK);
  assert(uart_driver_install(UART_NUM_1,256,0,0,nullptr,0)==ESP_OK);
  const int external_writes=test_uart_config_writes();
  assert(acquire_uart_port(UART_NUM_1,&owner_a,first,-1,5,256)==ESP_ERR_INVALID_STATE);
  assert(test_uart_config_writes()==external_writes);
  assert(release_uart_port(UART_NUM_1,&owner_a)==ESP_OK);
  assert(uart_is_driver_installed(UART_NUM_1));
  assert(uart_driver_delete(UART_NUM_1)==ESP_OK);
  for(int stage=1;stage<=3;++stage) {
    test_uart_fail_stage(stage);
    assert(acquire_uart_port(UART_NUM_1,&owner_a,first,-1,5,256)!=ESP_OK);
    assert(!uart_is_driver_installed(UART_NUM_1));
    test_uart_fail_stage(0);
    assert(acquire_uart_port(UART_NUM_1,&owner_b,second,7,8,256)==ESP_OK);
    assert(release_uart_port(UART_NUM_1,&owner_b)==ESP_OK);
  }
  assert(acquire_uart_port(static_cast<uart_port_t>(-1),&owner_a,first,-1,5,256)==ESP_ERR_INVALID_ARG);
  adc_oneshot_unit_handle_t a=nullptr,b=nullptr,c=nullptr;
  assert(acquire_shared_adc_oneshot_unit(ADC_UNIT_1,&a)==ESP_OK);
  assert(acquire_shared_adc_oneshot_unit(ADC_UNIT_1,&b)==ESP_OK);
  assert(a==b && creates==1);
  assert(acquire_shared_adc_oneshot_unit(ADC_UNIT_2,&c)==ESP_OK);
  assert(c!=a && creates==2);
  release_shared_adc_oneshot_unit(ADC_UNIT_1,c);
  release_shared_adc_oneshot_unit(ADC_UNIT_1,a);
  assert(deletes==0 && allocated[0]);
  release_shared_adc_oneshot_unit(ADC_UNIT_1,b);
  assert(deletes==1 && !allocated[0] && allocated[1]);
  release_shared_adc_oneshot_unit(ADC_UNIT_2,c);
  assert(deletes==2);
  fail_adc=true;
  assert(acquire_shared_adc_oneshot_unit(ADC_UNIT_1,&a)!=ESP_OK && a==nullptr);
  fail_adc=false;
  assert(acquire_shared_adc_oneshot_unit(ADC_UNIT_1,&a)==ESP_OK);
  release_shared_adc_oneshot_unit(ADC_UNIT_1,a);
  std::puts("ADC sharing and UART ownership/failure rollback verified");
}
