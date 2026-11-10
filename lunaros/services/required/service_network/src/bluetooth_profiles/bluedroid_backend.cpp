#include "bluetooth_internal.h"
#include "music_output.h"
#include "profile_retirement.h"
#include "network/bluetooth/bluetooth_lifecycle.h"
#include <sdkconfig.h>
#include <esp_timer.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <soc/soc_caps.h>
#if CONFIG_BT_CLASSIC_ENABLED && SOC_BT_CLASSIC_SUPPORTED
#define LUNAROS_BT_CLASSIC 1
#else
#define LUNAROS_BT_CLASSIC 0
#endif
#if LUNAROS_BT_CLASSIC && CONFIG_BT_SPP_ENABLED
#define LUNAROS_BT_SPP 1
#else
#define LUNAROS_BT_SPP 0
#endif
#if LUNAROS_BT_CLASSIC && CONFIG_BT_A2DP_ENABLE && CONFIG_BT_AVRCP_ENABLED && LUNAROS_NETWORK_BLUETOOTH_AUDIO
#define LUNAROS_BT_MUSIC 1
#else
#define LUNAROS_BT_MUSIC 0
#endif
#if CONFIG_LUNAROS_NETWORK_BLUETOOTH_PROFILES && CONFIG_BT_BLUEDROID_ENABLED && CONFIG_BT_BLE_ENABLED && CONFIG_BT_GATTS_ENABLE
#include <esp_bt_device.h>
#if LUNAROS_BT_CLASSIC
#include <esp_gap_bt_api.h>
#endif
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_gatt_common_api.h>
#if LUNAROS_BT_SPP
#include <esp_spp_api.h>
#endif
#if LUNAROS_BT_MUSIC
#include <esp_a2dp_api.h>
#include <esp_a2dp_legacy_api.h>
#include <esp_avrc_api.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lunaredu::lunaros::bluetooth_service_internal {
namespace {
uint32_t now(){return uint32_t(esp_timer_get_time()/1000);}
std::atomic<uint32_t> pair_until{0},key_until{0},spp_handle{0};
std::atomic<bool> encrypted{false},key_notifications{false},media_notifications{false},data_notifications{false};
std::atomic<bool> avrc_connected{false},music_playing{false};
uint8_t avrc_release=0,avrc_transaction=0;
std::atomic<esp_gatt_if_t> gatt_if{ESP_GATT_IF_NONE};
std::atomic<uint16_t> connection{0};
esp_bd_addr_t confirmation_peer{};
bool confirmation_ble=false;
std::atomic<uint16_t> hid_handles[17]{},data_handles[4]{};
std::atomic<bool> advertisement_ready{false};
uint16_t pending_release_handle=0,pending_release_size=0;
uint32_t release_started=0;
constexpr uint32_t kGattProfile=1,kSppProfile=2,kAvrcProfile=4,kA2dpProfile=8;
ProfileRetirement retirement;
std::atomic<bool> profile_stopping{false};
bool window(){const auto deadline=pair_until.load();return deadline&&int32_t(deadline-now())>0;}
bool checked(esp_err_t result,std::string &error){if(result==ESP_OK)return true;error=esp_err_to_name(result);return false;}
void changed(){++state.revision;}
bool known_ble(const uint8_t *peer){
  int count=esp_ble_get_bond_device_num();
  if(count<=0||count>16)return false;
  esp_ble_bond_dev_t list[16]{};
  if(esp_ble_get_bond_device_list(&count,list)!=ESP_OK)return false;
  for(int i=0;i<count;++i)if(!std::memcmp(peer,list[i].bd_addr,6))return true;
  return false;
}
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
esp_ble_gap_ext_adv_params_t advertisement{};
#else
esp_ble_adv_params_t advertisement{};
#endif
void advertise(){
  if(profile_stopping.load()||!advertisement_ready.load()||!hid_handles[0].load()||!data_handles[0].load()||!window())return;
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  esp_ble_gap_ext_adv_t set{};set.instance=0;set.duration=6000;
  (void)esp_ble_gap_ext_adv_start(1,&set);
#else
  (void)esp_ble_gap_start_advertising(&advertisement);
#endif
}
void stop_advertisement(){
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  const uint8_t instance=0;(void)esp_ble_gap_ext_adv_stop(1,&instance);
#else
  (void)esp_ble_gap_stop_advertising();
#endif
}
esp_err_t configure_advertisement_data(){
  uint8_t data[31]={2,ESP_BLE_AD_TYPE_FLAG,ESP_BLE_ADV_FLAG_GEN_DISC,
    3,ESP_BLE_AD_TYPE_16SRV_CMPL,0x12,0x18,3,ESP_BLE_AD_TYPE_APPEARANCE,0xc1,0x03};
  const size_t length=std::min(size_t(16),std::strlen(device_name.data()));
  data[11]=uint8_t(length+1);data[12]=ESP_BLE_AD_TYPE_NAME_SHORT;std::memcpy(data+13,device_name.data(),length);
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  return esp_ble_gap_config_ext_adv_data_raw(0,uint16_t(length+13),data);
#else
  return esp_ble_gap_config_adv_data_raw(data,uint32_t(length+13));
#endif
}
void read_name(BluetoothDevice &device,const uint8_t *data,size_t size){
  for(size_t offset=0;offset<size;){
    const size_t length=data[offset];if(!length||length>size-offset-1)break;
    if(data[offset+1]==ESP_BLE_AD_TYPE_NAME_CMPL||data[offset+1]==ESP_BLE_AD_TYPE_NAME_SHORT) {
      std::memcpy(device.name.data(),data+offset+2,std::min(length-1,device.name.size()-1));return;
    }
    offset+=length+1;
  }
}
void ble_gap(esp_gap_ble_cb_event_t event,esp_ble_gap_cb_param_t *p){
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  if(event==ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT){
    if(p->ext_adv_set_params.status!=ESP_BT_STATUS_SUCCESS||configure_advertisement_data()!=ESP_OK)report("bluetooth_advertisement_failed");
    return;
  }
  if(event==ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT){advertisement_ready=p->ext_adv_data_set.status==ESP_BT_STATUS_SUCCESS;advertise();return;}
  if(event==ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT){if(p->ext_adv_start.status!=ESP_BT_STATUS_SUCCESS)report("bluetooth_advertisement_start_failed");return;}
  if(event==ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT){
    if(p->set_ext_scan_params.status!=ESP_BT_STATUS_SUCCESS||esp_ble_gap_start_ext_scan(800,0)!=ESP_OK)report("bluetooth_scan_failed");
    return;
  }
  if(event==ESP_GAP_BLE_EXT_ADV_REPORT_EVT){
    const auto &report_data=p->ext_adv_report.params;
    BluetoothDevice d;std::copy_n(report_data.addr,6,d.address.begin());d.rssi=report_data.rssi;d.seen_ms=now();
    read_name(d,report_data.adv_data,report_data.adv_data_len);observe_device(d);return;
  }
  if(event==ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT&&p->ext_scan_start.status!=ESP_BT_STATUS_SUCCESS){
    {std::lock_guard<std::mutex> lock(mutex);state.discovering=false;changed();}
    report("bluetooth_scan_start_failed");return;
  }
  if(event==ESP_GAP_BLE_SCAN_TIMEOUT_EVT||event==ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT){std::lock_guard<std::mutex> lock(mutex);state.discovering=false;changed();return;}
#else
  if(event==ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT){advertisement_ready=p->adv_data_raw_cmpl.status==ESP_BT_STATUS_SUCCESS;advertise();return;}
  if(event==ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT){(void)esp_ble_gap_start_scanning(8);return;}
  if(event==ESP_GAP_BLE_SCAN_RESULT_EVT){
    if(p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_RES_EVT){
      BluetoothDevice d;std::copy_n(p->scan_rst.bda,6,d.address.begin());d.rssi=int8_t(p->scan_rst.rssi);d.seen_ms=now();
      read_name(d,p->scan_rst.ble_adv,size_t(p->scan_rst.adv_data_len)+p->scan_rst.scan_rsp_len);
      observe_device(d);
    }else if(p->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_CMPL_EVT){std::lock_guard<std::mutex> lock(mutex);state.discovering=false;changed();}
    return;
  }
#endif
  if(event==ESP_GAP_BLE_SEC_REQ_EVT){(void)esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr,window()||known_ble(p->ble_security.ble_req.bd_addr));return;}
  if(event==ESP_GAP_BLE_NC_REQ_EVT){
    if(!window()){(void)esp_ble_confirm_reply(p->ble_security.key_notif.bd_addr,false);return;}
    std::lock_guard<std::mutex> lock(mutex);std::memcpy(confirmation_peer,p->ble_security.key_notif.bd_addr,6);confirmation_ble=true;
    state.confirm_pending=true;state.pairing_code=p->ble_security.key_notif.passkey;changed();return;
  }
  if(event==ESP_GAP_BLE_AUTH_CMPL_EVT){
    encrypted=p->ble_security.auth_cmpl.success;
    std::lock_guard<std::mutex> lock(mutex);state.ble_encrypted=encrypted.load();state.confirm_pending=false;changed();
  }
}
#if LUNAROS_BT_CLASSIC
void classic_gap(esp_bt_gap_cb_event_t event,esp_bt_gap_cb_param_t *p){
  if(event==ESP_BT_GAP_DISC_RES_EVT){
    BluetoothDevice d;d.classic=true;d.seen_ms=now();std::copy_n(p->disc_res.bda,6,d.address.begin());
    for(int i=0;i<p->disc_res.num_prop;++i){const auto &v=p->disc_res.prop[i];
      if(v.type==ESP_BT_GAP_DEV_PROP_BDNAME&&v.val&&v.len>0)std::memcpy(d.name.data(),v.val,std::min(size_t(v.len),d.name.size()-1));
      if(v.type==ESP_BT_GAP_DEV_PROP_RSSI&&v.val)d.rssi=*static_cast<int8_t *>(v.val);
    }observe_device(d);return;
  }
  if(event==ESP_BT_GAP_DISC_STATE_CHANGED_EVT){std::lock_guard<std::mutex> lock(mutex);state.discovering=p->disc_st_chg.state==ESP_BT_GAP_DISCOVERY_STARTED;changed();return;}
  if(event==ESP_BT_GAP_CFM_REQ_EVT){
    if(!window()){(void)esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda,false);return;}
    std::lock_guard<std::mutex> lock(mutex);std::memcpy(confirmation_peer,p->cfm_req.bda,6);confirmation_ble=false;
    state.confirm_pending=true;state.pairing_code=p->cfm_req.num_val;changed();return;
  }
  if(event==ESP_BT_GAP_PIN_REQ_EVT){esp_bt_pin_code_t pin{};(void)esp_bt_gap_pin_reply(p->pin_req.bda,false,0,pin);return;}
  if(event==ESP_BT_GAP_AUTH_CMPL_EVT){std::lock_guard<std::mutex> lock(mutex);state.confirm_pending=false;changed();}
}
#endif
// HID service: keyboard report ID 1 and consumer report ID 2. Report references
// identify IDs; notification values do not contain the ID byte on BLE.
uint8_t report_map[]={
  0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,
  0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,
  0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0,
  0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,0x15,0x00,0x26,0xff,0x03,0x19,0x00,
  0x2a,0xff,0x03,0x75,0x10,0x95,0x01,0x81,0x00,0xc0};
uint16_t service_uuid=ESP_GATT_UUID_PRI_SERVICE,characteristic_uuid=ESP_GATT_UUID_CHAR_DECLARE,ccc_uuid=ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
uint16_t hid_uuid=0x1812,info_uuid=0x2a4a,map_uuid=0x2a4b,control_uuid=0x2a4c,protocol_uuid=0x2a4e,report_uuid=0x2a4d,reference_uuid=0x2908;
uint16_t data_service_uuid=0xfff0,data_uuid=0xfff1;
uint8_t read_property=ESP_GATT_CHAR_PROP_BIT_READ,write_property=ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
uint8_t read_write_property=ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
uint8_t notify_property=ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_NOTIFY;
uint8_t data_property=ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE|ESP_GATT_CHAR_PROP_BIT_NOTIFY;
uint8_t info[]={0x11,0x01,0x00,0x02},control=0,protocol=1,keyboard[8]{},consumer[2]{},ccc[2]{},key_reference[]={1,1},media_reference[]={2,1},data_value[160]{};
esp_gatts_attr_db_t attribute(uint16_t &uuid,uint16_t permission,uint16_t maximum,uint16_t length,void *value){
  return {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,reinterpret_cast<uint8_t *>(&uuid),permission,maximum,length,static_cast<uint8_t *>(value)}};
}
void create_tables(esp_gatt_if_t interface){
  const uint16_t encrypted_read=ESP_GATT_PERM_READ_ENCRYPTED,encrypted_write=ESP_GATT_PERM_WRITE_ENCRYPTED;
  esp_gatts_attr_db_t hid[]={
    attribute(service_uuid,ESP_GATT_PERM_READ,2,2,&hid_uuid),
    attribute(characteristic_uuid,ESP_GATT_PERM_READ,1,1,&read_property),attribute(info_uuid,ESP_GATT_PERM_READ,4,4,info),
    attribute(characteristic_uuid,ESP_GATT_PERM_READ,1,1,&read_property),attribute(map_uuid,ESP_GATT_PERM_READ,sizeof(report_map),sizeof(report_map),report_map),
    attribute(characteristic_uuid,ESP_GATT_PERM_READ,1,1,&write_property),attribute(control_uuid,encrypted_write,1,1,&control),
    attribute(characteristic_uuid,ESP_GATT_PERM_READ,1,1,&read_write_property),attribute(protocol_uuid,encrypted_read|encrypted_write,1,1,&protocol),
    attribute(characteristic_uuid,ESP_GATT_PERM_READ,1,1,&notify_property),attribute(report_uuid,encrypted_read,8,8,keyboard),
    attribute(ccc_uuid,encrypted_read|encrypted_write,2,2,ccc),attribute(reference_uuid,ESP_GATT_PERM_READ,2,2,key_reference),
    attribute(characteristic_uuid,ESP_GATT_PERM_READ,1,1,&notify_property),attribute(report_uuid,encrypted_read,2,2,consumer),
    attribute(ccc_uuid,encrypted_read|encrypted_write,2,2,ccc),attribute(reference_uuid,ESP_GATT_PERM_READ,2,2,media_reference)};
  esp_gatts_attr_db_t data[]={attribute(service_uuid,ESP_GATT_PERM_READ,2,2,&data_service_uuid),
    attribute(characteristic_uuid,ESP_GATT_PERM_READ,1,1,&data_property),attribute(data_uuid,encrypted_read|encrypted_write,160,0,data_value),
    attribute(ccc_uuid,encrypted_read|encrypted_write,2,2,ccc)};
  auto a=esp_ble_gatts_create_attr_tab(hid,interface,std::size(hid),0);
  auto b=esp_ble_gatts_create_attr_tab(data,interface,std::size(data),1);
  if(a!=ESP_OK||b!=ESP_OK)report("bluetooth_gatt_table_failed");
}
void gatt(esp_gatts_cb_event_t event,esp_gatt_if_t interface,esp_ble_gatts_cb_param_t *p){
  if(event==ESP_GATTS_UNREG_EVT){gatt_if=ESP_GATT_IF_NONE;retirement.completed(kGattProfile);return;}
  if(event==ESP_GATTS_REG_EVT){if(p->reg.status!=ESP_GATT_OK){retirement.completed(kGattProfile);report("bluetooth_gatt_registration_failed");return;}gatt_if=interface;if(!profile_stopping.load())create_tables(interface);return;}
  if(profile_stopping.load())return;
  if(event==ESP_GATTS_CREAT_ATTR_TAB_EVT){
    if(p->add_attr_tab.status!=ESP_GATT_OK){report("bluetooth_gatt_table_failed");return;}
    if(p->add_attr_tab.num_handle==17)for(size_t i=0;i<17;++i)hid_handles[i]=p->add_attr_tab.handles[i];
    else if(p->add_attr_tab.num_handle==4)for(size_t i=0;i<4;++i)data_handles[i]=p->add_attr_tab.handles[i];
    (void)esp_ble_gatts_start_service(p->add_attr_tab.handles[0]);advertise();return;
  }
  if(event==ESP_GATTS_CONNECT_EVT){
    bool occupied=false;{std::lock_guard<std::mutex> lock(mutex);occupied=state.ble_connected;}
    if(occupied){(void)esp_ble_gap_disconnect(p->connect.remote_bda);return;}
    connection=p->connect.conn_id;
    if(!window()&&!known_ble(p->connect.remote_bda)){(void)esp_ble_gap_disconnect(p->connect.remote_bda);return;}
    (void)esp_ble_set_encryption(p->connect.remote_bda,ESP_BLE_SEC_ENCRYPT_MITM);
    std::lock_guard<std::mutex> lock(mutex);state.ble_connected=true;changed();return;
  }
  if(event==ESP_GATTS_DISCONNECT_EVT){
    if(p->disconnect.conn_id!=connection.load())return;
    encrypted=false;key_notifications=false;media_notifications=false;data_notifications=false;key_until=0;
    {std::lock_guard<std::mutex> lock(mutex);state.ble_connected=false;state.ble_encrypted=false;state.keyboard_armed=false;changed();}advertise();return;
  }
  if(event==ESP_GATTS_WRITE_EVT){
    if(p->write.is_prep||!encrypted.load())return;
    if(p->write.handle==data_handles[2])receive(p->write.value,p->write.len);
    if(p->write.len==2){const bool notify=p->write.value[0]==1&&p->write.value[1]==0;
      if(p->write.handle==hid_handles[11])key_notifications=notify;
      if(p->write.handle==hid_handles[15])media_notifications=notify;
      if(p->write.handle==data_handles[3])data_notifications=notify;
    }
  }
}
#if LUNAROS_BT_SPP
void spp(esp_spp_cb_event_t event,esp_spp_cb_param_t *p){
  if(event==ESP_SPP_UNINIT_EVT){if(p->uninit.status==ESP_SPP_SUCCESS)retirement.completed(kSppProfile);else retirement.failed(kSppProfile);return;}
  if(profile_stopping.load())return;
  if(event==ESP_SPP_INIT_EVT){if(p->init.status!=ESP_SPP_SUCCESS){report("bluetooth_spp_init_failed");return;}(void)esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE|ESP_SPP_SEC_ENCRYPT,ESP_SPP_ROLE_SLAVE,0,"LunarOS Data");return;}
  if(event==ESP_SPP_SRV_OPEN_EVT){spp_handle=p->srv_open.handle;std::lock_guard<std::mutex> lock(mutex);state.spp_connected=true;changed();return;}
  if(event==ESP_SPP_CLOSE_EVT){spp_handle=0;std::lock_guard<std::mutex> lock(mutex);state.spp_connected=false;changed();return;}
  if(event==ESP_SPP_DATA_IND_EVT)receive(p->data_ind.data,p->data_ind.len);
}
#endif
#if LUNAROS_BT_MUSIC
void a2dp(esp_a2d_cb_event_t event,esp_a2d_cb_param_t *p){
  if(event==ESP_A2D_PROF_STATE_EVT&&p->a2d_prof_stat.init_state==ESP_A2D_DEINIT_SUCCESS){retirement.completed(kA2dpProfile);return;}
  if(profile_stopping.load())return;
  if(event==ESP_A2D_AUDIO_STATE_EVT){music_playing=p->audio_stat.state==ESP_A2D_AUDIO_STATE_STARTED;std::lock_guard<std::mutex> lock(mutex);state.music_playing=music_playing.load();changed();}
  if(event==ESP_A2D_CONNECTION_STATE_EVT){std::lock_guard<std::mutex> lock(mutex);state.music_connected=p->conn_stat.state==ESP_A2D_CONNECTION_STATE_CONNECTED;if(!state.music_connected){state.music_playing=false;music_playing=false;}changed();}
  if(event==ESP_A2D_AUDIO_CFG_EVT&&p->audio_cfg.mcc.type==ESP_A2D_MCT_SBC){
    const auto &sbc=p->audio_cfg.mcc.cie.sbc_info;
    music_audio_format(sbc.samp_freq&ESP_A2D_SBC_CIE_SF_48K?48000:sbc.samp_freq&ESP_A2D_SBC_CIE_SF_44K?44100:
      sbc.samp_freq&ESP_A2D_SBC_CIE_SF_32K?32000:16000,sbc.ch_mode&ESP_A2D_SBC_CIE_CH_MODE_MONO?1:2);
  }
}
void avrc(esp_avrc_ct_cb_event_t event,esp_avrc_ct_cb_param_t *p){
  if(event==ESP_AVRC_CT_PROF_STATE_EVT){
    if(p->avrc_ct_init_stat.state==ESP_AVRC_DEINIT_SUCCESS||p->avrc_ct_init_stat.state==ESP_AVRC_DEINIT_ALREADY)retirement.completed(kAvrcProfile);
    else if(p->avrc_ct_init_stat.state==ESP_AVRC_DEINIT_FAIL)retirement.failed(kAvrcProfile);
    return;
  }
  if(!profile_stopping.load()&&event==ESP_AVRC_CT_CONNECTION_STATE_EVT){avrc_connected=p->conn_stat.connected;std::lock_guard<std::mutex> lock(mutex);state.music_control_available=avrc_connected.load();changed();}
}
#endif
bool install(void *,std::string &error){
  if(retirement.pending()){error="bluetooth_profile_teardown_pending";return false;}
  profile_stopping=false;(void)retirement.take_failure();
  gatt_if=ESP_GATT_IF_NONE;advertisement_ready=false;
  for(auto &handle:hid_handles)handle=0;
  for(auto &handle:data_handles)handle=0;
  if(!checked(esp_ble_gap_register_callback(ble_gap),error)||
     !checked(esp_ble_gatts_register_callback(gatt),error))return false;
  retirement.acquired(kGattProfile);
  if(!checked(esp_ble_gatts_app_register(0x31),error)){retirement.completed(kGattProfile);return false;}
#if LUNAROS_BT_CLASSIC
  if(!checked(esp_bt_gap_register_callback(classic_gap),error))return false;
  esp_bt_io_cap_t classic_io=ESP_BT_IO_CAP_IO;
  if(!checked(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE,&classic_io,sizeof(classic_io)),error)||
     !checked(esp_bt_gap_set_device_name(device_name.data()),error)||
     !checked(esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE,ESP_BT_NON_DISCOVERABLE),error))return false;
#endif
#if LUNAROS_BT_SPP
  if(!checked(esp_spp_register_callback(spp),error))return false;
  esp_spp_cfg_t spp_configuration{};spp_configuration.mode=ESP_SPP_MODE_CB;spp_configuration.enable_l2cap_ertm=true;spp_configuration.tx_buffer_size=512;
  retirement.acquired(kSppProfile);
  if(!checked(esp_spp_enhanced_init(&spp_configuration),error)){retirement.completed(kSppProfile);return false;}
#endif
#if LUNAROS_BT_MUSIC
  if(!checked(esp_a2d_register_callback(a2dp),error)||!checked(esp_a2d_sink_register_data_callback(music_receive_pcm),error)||
     !checked(esp_avrc_ct_register_callback(avrc),error))return false;
  retirement.acquired(kAvrcProfile);
  if(!checked(esp_avrc_ct_init(),error)){retirement.completed(kAvrcProfile);return false;}
  retirement.acquired(kA2dpProfile);
  if(!checked(esp_a2d_sink_init(),error)){retirement.completed(kA2dpProfile);return false;}
#endif
  uint8_t ble_io=ESP_IO_CAP_IO,key_size=16,strict_auth=ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_ENABLE;
  esp_ble_auth_req_t auth=ESP_LE_AUTH_REQ_SC_MITM_BOND;
  if(!checked(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,&auth,sizeof(auth)),error)||
     !checked(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,&ble_io,sizeof(ble_io)),error)||
     !checked(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,&key_size,sizeof(key_size)),error)||
     !checked(esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH,&strict_auth,sizeof(strict_auth)),error)||
     !checked(esp_ble_gap_set_device_name(device_name.data()),error))return false;
  advertisement.own_addr_type=BLE_ADDR_TYPE_PUBLIC;advertisement.channel_map=ADV_CHNL_ALL;
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  advertisement.type=ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND;advertisement.interval_min=0x60;advertisement.interval_max=0x100;
  advertisement.filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;advertisement.primary_phy=ESP_BLE_GAP_PRI_PHY_1M;advertisement.secondary_phy=ESP_BLE_GAP_PHY_1M;
  return checked(esp_ble_gap_ext_adv_set_params(0,&advertisement),error);
#else
  advertisement.adv_int_min=0x60;advertisement.adv_int_max=0x100;advertisement.adv_type=ADV_TYPE_IND;
  advertisement.adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
  return checked(configure_advertisement_data(),error);
#endif
}
void reset(void *){
  set_request_admission(false);
  pending_release_handle=0;avrc_release=0;
  pair_until=key_until=spp_handle=0;encrypted=false;avrc_connected=false;music_playing=false;key_notifications=media_notifications=data_notifications=false;gatt_if=ESP_GATT_IF_NONE;
  std::lock_guard<std::mutex> lock(mutex);state.enabled=false;state.discovering=false;state.pairing=false;
  state.ble_connected=state.ble_encrypted=state.spp_connected=state.music_connected=state.keyboard_armed=state.confirm_pending=state.music_enabled=false;changed();
  state.music_playing=state.music_control_available=false;
}
void synchronized(void *){set_request_admission(true);std::lock_guard<std::mutex> lock(mutex);state.enabled=true;changed();}
bool connected(void *){std::lock_guard<std::mutex> lock(mutex);return state.ble_connected||state.spp_connected||state.music_connected;}
bool prepare_stop(void *,std::string &error){
  set_request_admission(false);
  profile_stopping=true;
  if(!enable_music_output(false,error))return false;
  const bool advertised=pair_until.exchange(0)!=0;
  key_until=0;if(advertised)stop_advertisement();
  (void)retirement.take_failure();
  const uint32_t deadline=now()+2000;
  [[maybe_unused]] const auto close=[&](uint32_t profile,esp_err_t (*operation)()){
    if(!retirement.begin_close(profile))return true;
    if(checked(operation(),error))return true;
    retirement.failed(profile);return false;
  };
  do {
    // The host still owns its BTC/BTU threads here. Complete every protocol's
    // deinit callback before Network is permitted to disable those threads.
#if LUNAROS_BT_MUSIC
    if(!close(kA2dpProfile,esp_a2d_sink_deinit)||!close(kAvrcProfile,esp_avrc_ct_deinit))return false;
#endif
#if LUNAROS_BT_SPP
    if(!close(kSppProfile,esp_spp_deinit))return false;
#endif
    const auto interface=gatt_if.load();
    if(interface!=ESP_GATT_IF_NONE&&retirement.begin_close(kGattProfile)){
      if(!checked(esp_ble_gatts_app_unregister(interface),error)){retirement.failed(kGattProfile);return false;}
    }
    if(retirement.take_failure()){error="bluetooth_profile_stop_failed";return false;}
    if(!retirement.pending()){error.clear();return true;}
    vTaskDelay(pdMS_TO_TICKS(5));
  } while(int32_t(deadline-now())>0);
  error="bluetooth_profile_teardown_pending";return false;
}
void action(const Request &request){
  std::string error;const auto type=request.action;
  if(!bluetooth_running())error="bluetooth_disabled";
  else if(type==BluetoothAction::Pair){
    pair_until=now()+60000;advertise();
#if LUNAROS_BT_CLASSIC
    (void)esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,ESP_BT_GENERAL_DISCOVERABLE);
#endif
    std::lock_guard<std::mutex> lock(mutex);state.pairing=true;changed();
  }else if(type==BluetoothAction::ScanBle){
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
    esp_ble_ext_scan_params_t parameters{};parameters.own_addr_type=BLE_ADDR_TYPE_PUBLIC;
    parameters.filter_policy=BLE_SCAN_FILTER_ALLOW_ALL;parameters.scan_duplicate=BLE_SCAN_DUPLICATE_ENABLE;
    parameters.cfg_mask=ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK;parameters.uncoded_cfg={BLE_SCAN_TYPE_ACTIVE,0x80,0x40};
    (void)checked(esp_ble_gap_set_ext_scan_params(&parameters),error);
#else
    esp_ble_scan_params_t parameters{};parameters.scan_type=BLE_SCAN_TYPE_ACTIVE;parameters.own_addr_type=BLE_ADDR_TYPE_PUBLIC;
    parameters.scan_filter_policy=BLE_SCAN_FILTER_ALLOW_ALL;parameters.scan_interval=0x80;parameters.scan_window=0x40;parameters.scan_duplicate=BLE_SCAN_DUPLICATE_ENABLE;
    (void)checked(esp_ble_gap_set_scan_params(&parameters),error);
#endif
    std::lock_guard<std::mutex> lock(mutex);state.discovering=error.empty();changed();
  }else if(type==BluetoothAction::ScanClassic) {
#if LUNAROS_BT_CLASSIC
    (void)checked(esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,8,0),error);
#else
    error="bluetooth_action_unsupported";
#endif
  }
  else if(type==BluetoothAction::ConfirmPair||type==BluetoothAction::RejectPair){
    bool pending,ble;esp_bd_addr_t peer{};
    {std::lock_guard<std::mutex> lock(mutex);pending=state.confirm_pending;ble=confirmation_ble;
      std::memcpy(peer,confirmation_peer,6);state.confirm_pending=false;changed();}
    if(!pending||!window())error="bluetooth_pair_confirmation_expired";
    else if(ble)(void)checked(esp_ble_confirm_reply(peer,type==BluetoothAction::ConfirmPair),error);
#if LUNAROS_BT_CLASSIC
    else(void)checked(esp_bt_gap_ssp_confirm_reply(peer,type==BluetoothAction::ConfirmPair),error);
#endif
  }else if(type==BluetoothAction::ArmKeyboard||type==BluetoothAction::DisarmKeyboard){
    const bool arm=type==BluetoothAction::ArmKeyboard;
    if(arm&&!encrypted.load())error="bluetooth_encrypted_peer_required";
    else {key_until=arm?now()+60000:0;std::lock_guard<std::mutex> lock(mutex);state.keyboard_armed=arm;changed();}
  }else if(type==BluetoothAction::Key||type==BluetoothAction::Media||type==BluetoothAction::MusicMedia){
#if LUNAROS_BT_MUSIC
    bool music_allowed=false;{std::lock_guard<std::mutex> lock(mutex);music_allowed=state.music_enabled&&(type!=BluetoothAction::MusicMedia||state.music_connected);}
    if((type==BluetoothAction::Media||type==BluetoothAction::MusicMedia)&&avrc_connected.load()&&music_allowed) {
      if(avrc_release)error="bluetooth_media_release_pending";
      else {
        const uint8_t command=request.value==0xcd?(music_playing.load()?ESP_AVRC_PT_CMD_PAUSE:ESP_AVRC_PT_CMD_PLAY):
          request.value==0xb5?ESP_AVRC_PT_CMD_FORWARD:request.value==0xb6?ESP_AVRC_PT_CMD_BACKWARD:
          request.value==0xe9?ESP_AVRC_PT_CMD_VOL_UP:request.value==0xea?ESP_AVRC_PT_CMD_VOL_DOWN:ESP_AVRC_PT_CMD_MUTE;
        avrc_transaction=uint8_t((avrc_transaction+1)%16);
        if(checked(esp_avrc_ct_send_passthrough_cmd(avrc_transaction,command,ESP_AVRC_PT_CMD_STATE_PRESSED),error))avrc_release=command;
      }
    }
    else
#endif
    if(type==BluetoothAction::MusicMedia)error="bluetooth_music_peer_required";
    else if(pending_release_handle)error="bluetooth_key_release_pending";
    else if(!encrypted.load()||!key_until.load()||int32_t(key_until.load()-now())<=0)error="bluetooth_keyboard_not_armed";
    else if(type==BluetoothAction::Key&&!key_notifications.load())error="bluetooth_keyboard_not_subscribed";
    else if(type==BluetoothAction::Media&&!media_notifications.load())error="bluetooth_media_not_subscribed";
    else {
      uint8_t report_data[8]{};const bool key=type==BluetoothAction::Key;
      if(key){if(request.value>=0xe0)report_data[0]=uint8_t(1u<<(request.value-0xe0));else report_data[2]=uint8_t(request.value);}
      else {report_data[0]=uint8_t(request.value);report_data[1]=uint8_t(request.value>>8);}
      const uint16_t handle=hid_handles[key?10:14].load();const uint16_t count=key?8:2;
      if(checked(esp_ble_gatts_send_indicate(gatt_if.load(),connection.load(),handle,count,report_data,false),error)) {
        pending_release_handle=handle;pending_release_size=count;release_started=now();
      }
    }
  }else if(type==BluetoothAction::SendData){
    const auto size=std::strlen(request.data.data());
#if LUNAROS_BT_SPP
    if(spp_handle.load())(void)checked(esp_spp_write(spp_handle.load(),int(size),reinterpret_cast<uint8_t *>(const_cast<char *>(request.data.data()))),error);
    else
#endif
    if(encrypted.load()&&data_notifications.load())(void)checked(esp_ble_gatts_send_indicate(gatt_if,connection,data_handles[2],uint16_t(size),reinterpret_cast<uint8_t *>(const_cast<char *>(request.data.data())),false),error);
    else error="bluetooth_data_peer_required";
    if(error.empty()){std::lock_guard<std::mutex> lock(mutex);state.sent_bytes+=uint32_t(size);changed();}
  }else if(type==BluetoothAction::MusicOn||type==BluetoothAction::MusicOff){
    const bool enable=type==BluetoothAction::MusicOn;
    if(enable_music_output(enable,error)){std::lock_guard<std::mutex> lock(mutex);state.music_enabled=enable;changed();}
  }
  report(error);
}
void poll(void *){
#if LUNAROS_BT_MUSIC
  if(avrc_release) {
    if(!bluetooth_running()||!avrc_connected.load())avrc_release=0;
    else if(esp_avrc_ct_send_passthrough_cmd(avrc_transaction,avrc_release,ESP_AVRC_PT_CMD_STATE_RELEASED)==ESP_OK)avrc_release=0;
  }
#endif
  if(pending_release_handle) {
    uint8_t zero[8]{};
    if(!bluetooth_running()||!encrypted.load())pending_release_handle=0;
    else if(esp_ble_gatts_send_indicate(gatt_if.load(),connection.load(),pending_release_handle,pending_release_size,zero,false)==ESP_OK)pending_release_handle=0;
    else if(now()-release_started>1000)report("bluetooth_key_release_pending");
  }
  const auto deadline=pair_until.load();
  if(deadline&&int32_t(deadline-now())<=0){
    pair_until=0;stop_advertisement();
#if LUNAROS_BT_CLASSIC
    (void)esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,ESP_BT_NON_DISCOVERABLE);
#endif
    std::lock_guard<std::mutex> lock(mutex);state.pairing=false;state.confirm_pending=false;changed();
  }
  const auto armed=key_until.load();
  if(armed&&int32_t(armed-now())<=0){key_until=0;std::lock_guard<std::mutex> lock(mutex);state.keyboard_armed=false;changed();}
  Request request;if(take_request(request))action(request);
}
}
bool configure_backend(std::string &error){
  static const BluetoothProfile profile{"bluetooth.apps",install,synchronized,reset,nullptr,nullptr,poll,connected,prepare_stop};
  if(!register_bluetooth_profile(profile,error))return false;
  std::lock_guard<std::mutex> lock(mutex);
  state.ble_supported=true;state.classic_supported=LUNAROS_BT_CLASSIC;
  state.music_supported=LUNAROS_BT_MUSIC;
  state.ble_scan_supported=true;state.pairing_supported=true;
  state.hid_supported=true;state.data_supported=true;
  return true;
}
} // namespace lunaredu::lunaros::bluetooth_service_internal
#elif !CONFIG_BT_NIMBLE_ENABLED || !CONFIG_LUNAROS_NETWORK_BLUETOOTH_PROFILES
namespace lunaredu::lunaros::bluetooth_service_internal {
bool configure_backend(std::string &error){error.clear();return true;}
}
#endif
