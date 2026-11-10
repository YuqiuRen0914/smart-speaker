#pragma once
#include "bluetooth_stub.h"
#include <cstddef>
#include <cstdint>
constexpr int ESP_FAIL=-1, ESP_MAC_BT=1, ESP_MAC_BASE=2;
constexpr int BLE_HS_CONN_HANDLE_NONE=65535, BLE_HS_EALREADY=2, BLE_HS_ENOTCONN=3;
constexpr int BLE_GAP_EVENT_CONNECT=1, BLE_GAP_EVENT_DISCONNECT=2, BLE_GAP_EVENT_ADV_COMPLETE=3;
constexpr int BLE_ERR_REM_USER_CONN_TERM=19, BLE_HS_FOREVER=0, BLE_OWN_ADDR_PUBLIC=0;
constexpr int BLE_HS_ADV_F_DISC_GEN=1, BLE_HS_ADV_F_BREDR_UNSUP=2;
constexpr int BLE_GAP_CONN_MODE_UND=1, BLE_GAP_DISC_MODE_GEN=1;
constexpr int BLE_ATT_ERR_UNLIKELY=1, BLE_ATT_ERR_INSUFFICIENT_RES=2;
constexpr int BLE_GATT_ACCESS_OP_READ_CHR=1, BLE_GATT_ACCESS_OP_WRITE_CHR=2;
constexpr int BLE_GATT_CHR_F_WRITE=1, BLE_GATT_CHR_F_WRITE_NO_RSP=2, BLE_GATT_CHR_F_WRITE_ENC=4;
constexpr int BLE_GATT_CHR_F_READ=8, BLE_GATT_CHR_F_READ_ENC=16, BLE_GATT_CHR_F_NOTIFY=32;
constexpr int BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC=64, BLE_GATT_SVC_TYPE_PRIMARY=1;
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
struct ble_uuid_t {uint8_t type=0;};
struct ble_uuid128_t {ble_uuid_t u;uint8_t value[16];};
#define BLE_UUID128_INIT(...) { {}, {__VA_ARGS__} }
struct os_mbuf {uint16_t length=0;};
#define OS_MBUF_PKTLEN(om) ((om)->length)
struct ble_gap_conn_desc {struct {int encrypted=1;} sec_state;};
struct ble_gap_event {int type;struct {int status;uint16_t conn_handle;} connect;};
struct ble_gatt_access_ctxt {int op;os_mbuf *om;};
struct ble_gatt_chr_def {
 const ble_uuid_t *uuid=nullptr;
 int (*access_cb)(uint16_t,uint16_t,ble_gatt_access_ctxt*,void*)=nullptr;
 int flags=0;uint16_t *val_handle=nullptr;
};
struct ble_gatt_svc_def {int type=0;const ble_uuid_t *uuid=nullptr;ble_gatt_chr_def *characteristics=nullptr;};
struct ble_hs_adv_fields {int flags=0;const uint8_t *name=nullptr;size_t name_len=0;bool name_is_complete=false;ble_uuid128_t *uuids128=nullptr;int num_uuids128=0;bool uuids128_is_complete=false;};
struct ble_gap_adv_params {int conn_mode=0,disc_mode=0,itvl_min=0,itvl_max=0;};
int esp_read_mac(uint8_t*,int);
int64_t esp_timer_get_time();
int ble_gap_conn_find(uint16_t,ble_gap_conn_desc*);
os_mbuf *ble_hs_mbuf_from_flat(const void*,size_t);
int ble_gatts_notify_custom(uint16_t,uint16_t,os_mbuf*);
int ble_hs_mbuf_to_flat(os_mbuf*,void*,uint16_t,uint16_t*);
int os_mbuf_append(os_mbuf*,const void*,size_t);
int ble_gap_adv_active();
int ble_gap_adv_rsp_set_fields(const ble_hs_adv_fields*);
int ble_gap_adv_set_fields(const ble_hs_adv_fields*);
int ble_gap_adv_start(int,const void*,int,const ble_gap_adv_params*,int(*)(ble_gap_event*,void*),void*);
int ble_gap_adv_stop();
int ble_gap_terminate(uint16_t,int);
int ble_gatts_count_cfg(ble_gatt_svc_def*);
int ble_gatts_add_svcs(ble_gatt_svc_def*);
int ble_svc_gap_device_name_set(const char*);
