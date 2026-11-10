#pragma once
#include <cstdint>
using wifi_ps_type_t = int;
constexpr int WIFI_PS_MIN_MODEM = 1;
using wifi_mode_t = int;
constexpr int WIFI_MODE_STA = 1, WIFI_MODE_APSTA = 3;
constexpr int WIFI_IF_STA = 0, WIFI_ALL_CHANNEL_SCAN = 1, WIFI_CONNECT_AP_BY_SIGNAL = 1;
constexpr int WIFI_AUTH_OPEN = 0, WIFI_SCAN_TYPE_ACTIVE = 0;
constexpr int ESP_ERR_WIFI_CONN = 100;
struct wifi_config_t {
  struct { uint8_t ssid[32], password[64]; int scan_method, sort_method;
    struct { int rssi; } threshold; int failure_retry_cnt; } sta;
};
struct wifi_scan_config_t { bool show_hidden; int scan_type; };
struct wifi_ap_record_t { uint8_t ssid[33]; int rssi, authmode; uint8_t primary; };
using wifi_second_chan_t = int;
constexpr int WIFI_SECOND_CHAN_NONE = 0;
int esp_wifi_set_mode(wifi_mode_t);
int esp_wifi_start();
int esp_wifi_disconnect();
int esp_wifi_set_config(int, const wifi_config_t *);
int esp_wifi_connect();
int esp_wifi_scan_start(const wifi_scan_config_t *, bool);
int esp_wifi_scan_get_ap_num(uint16_t *);
int esp_wifi_scan_get_ap_records(uint16_t *, wifi_ap_record_t *);
int esp_wifi_get_channel(uint8_t *, wifi_second_chan_t *);
int esp_wifi_set_channel(uint8_t, wifi_second_chan_t);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
