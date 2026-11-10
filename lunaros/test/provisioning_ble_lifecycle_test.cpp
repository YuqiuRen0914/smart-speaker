#include "provisioning_ble_stub.h"
#include "lunaredu_lunaros/network/provisioning_ble.h"
#include "network/bluetooth/bluetooth.h"
#include <cassert>
#include <iostream>
BleConfig ble_hs_cfg;
int64_t clock_ms=0;
bool advertising=false,fail_adv=false,fail_stop=false,fail_disconnect=false,fail_host=false;
int starts=0,adv_attempts=0,terminate_attempts=0;
int (*gap_callback)(ble_gap_event*,void*)=nullptr;
const char *esp_err_to_name(int){return "host_failure";}
int nimble_port_init(){++starts;return fail_host ? 1 : 0;}
int nimble_port_deinit(){return 0;}
int nimble_port_stop(){advertising=false;return 0;}
void nimble_port_run(){}
int esp_nimble_enable(void*){return 0;}
void nimble_port_freertos_deinit(){}
void ble_svc_gap_init(){}
void ble_svc_gatt_init(){}
int ble_store_util_status_rr(int,void*){return 0;}
extern "C" void ble_store_config_init(){}
int esp_read_mac(uint8_t*,int){return 0;}
int64_t esp_timer_get_time(){return clock_ms*1000;}
int ble_gap_conn_find(uint16_t,ble_gap_conn_desc*){return 0;}
os_mbuf *ble_hs_mbuf_from_flat(const void*,size_t){return nullptr;}
int ble_gatts_notify_custom(uint16_t,uint16_t,os_mbuf*){return 0;}
int ble_hs_mbuf_to_flat(os_mbuf*,void*,uint16_t,uint16_t*){return 1;}
int os_mbuf_append(os_mbuf*,const void*,size_t){return 0;}
int ble_gap_adv_active(){return advertising;}
int ble_gap_adv_rsp_set_fields(const ble_hs_adv_fields*){return 0;}
int ble_gap_adv_set_fields(const ble_hs_adv_fields*){return 0;}
int ble_gap_adv_start(int,const void*,int,const ble_gap_adv_params*,int(*cb)(ble_gap_event*,void*),void*){
 ++adv_attempts;gap_callback=cb;if(fail_adv)return 1;advertising=true;return 0;
}
int ble_gap_adv_stop(){if(fail_stop)return 1;advertising=false;return 0;}
int ble_gap_terminate(uint16_t,int){++terminate_attempts;return fail_disconnect?1:0;}
int ble_gatts_count_cfg(ble_gatt_svc_def*){return 0;}
int ble_gatts_add_svcs(ble_gatt_svc_def*){return 0;}
int ble_svc_gap_device_name_set(const char*){return 0;}
int main(){
 using namespace lunaredu::lunaros;using State=ProvisioningBleState;
 std::string error;ProvisioningBleConfig config;
 assert(provisioning_ble_start(config,error));
 assert(!provisioning_ble_running());
 assert(provisioning_ble_snapshot().state==State::WaitingForHost);
 ble_hs_cfg.sync_cb();fail_adv=true;clock_ms+=250;
 assert(!provisioning_ble_poll(error) && error=="ble_advertising_failed");
 assert(provisioning_ble_snapshot().state==State::Failed);
 const int failed_attempts=adv_attempts;
 assert(!provisioning_ble_poll(error) && adv_attempts==failed_attempts);
 fail_adv=false;assert(provisioning_ble_start(config,error));
 assert(provisioning_ble_running() && provisioning_ble_snapshot().state==State::Advertising);
 fail_stop=true;assert(!provisioning_ble_stop(error));
 assert(provisioning_ble_snapshot().state==State::Stopping && advertising);
 fail_stop=false;clock_ms+=250;assert(provisioning_ble_poll(error) && !advertising);
 assert(provisioning_ble_start(config,error));
 ble_gap_event connected{BLE_GAP_EVENT_CONNECT,{0,42}};
 advertising=false;gap_callback(&connected,nullptr);
 assert(provisioning_ble_snapshot().state==State::Connected);
 fail_disconnect=true;assert(!provisioning_ble_stop(error));
 assert(!provisioning_ble_stop(error) && terminate_attempts==2);
 fail_disconnect=false;clock_ms+=250;
 assert(!provisioning_ble_poll(error) && error=="ble_disconnect_pending");
 assert(!provisioning_ble_start(config,error));
 assert(terminate_attempts==3); // accepted disconnect is not submitted twice
 ble_gap_event disconnected{BLE_GAP_EVENT_DISCONNECT,{0,0}};
 gap_callback(&disconnected,nullptr);clock_ms+=250;
 assert(provisioning_ble_poll(error));
 assert(provisioning_ble_start(config,error));
 assert(stop_bluetooth(error));assert(!provisioning_ble_running());
 assert(provisioning_ble_snapshot().state==State::Stopped);
 fail_host=true;assert(!provisioning_ble_start(config,error));
 fail_host=false;clock_ms+=250;assert(provisioning_ble_poll(error));
 assert(provisioning_ble_snapshot().state==State::WaitingForHost);
 ble_hs_cfg.sync_cb();clock_ms+=250;assert(provisioning_ble_poll(error));
 assert(provisioning_ble_running());
 advertising=false;ble_hs_cfg.reset_cb(1);assert(!provisioning_ble_running());
 ble_hs_cfg.sync_cb();clock_ms+=250;assert(provisioning_ble_poll(error));
 assert(provisioning_ble_running());
 assert(provisioning_ble_stop(error));
 // A connection accepted concurrently with stop must still be cleaned up.
 gap_callback(&connected,nullptr);clock_ms+=250;assert(!provisioning_ble_poll(error));
 gap_callback(&disconnected,nullptr);clock_ms+=250;assert(provisioning_ble_poll(error));
 assert(!provisioning_ble_running());
 std::cout<<"BLE provisioning: actual profile/host startup, backoff, advertising, stop, disconnect and reset recovery passed\n";
}
