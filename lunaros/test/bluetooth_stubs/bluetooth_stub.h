#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK=0;
constexpr int BLE_SM_IO_CAP_NO_IO=0;
constexpr int BLE_SM_PAIR_KEY_DIST_ID=1;
constexpr int BLE_SM_PAIR_KEY_DIST_ENC=2;
struct BleConfig {
 void (*sync_cb)() = nullptr;
 void (*reset_cb)(int) = nullptr;
 int (*store_status_cb)(int, void*) = nullptr;
 int sm_io_cap=0, sm_sc=0, sm_bonding=0, sm_mitm=0, sm_our_key_dist=0, sm_their_key_dist=0;
};
extern BleConfig ble_hs_cfg;
const char *esp_err_to_name(int);
int nimble_port_init();
int nimble_port_deinit();
int nimble_port_stop();
void nimble_port_run();
int esp_nimble_enable(void *);
void nimble_port_freertos_deinit();
void ble_svc_gap_init();
void ble_svc_gatt_init();
int ble_store_util_status_rr(int,void *);
