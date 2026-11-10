#pragma once
#include <cstdint>
using wifi_interface_t = int;
inline constexpr int WIFI_IF_STA = 0;
struct wifi_tx_info_t {};
struct wifi_pkt_rx_ctrl_t { int rssi; };
int esp_wifi_get_mac(wifi_interface_t, uint8_t *);
int esp_wifi_set_max_tx_power(int8_t);
int esp_wifi_get_max_tx_power(int8_t *);

using wifi_ps_type_t = int;
inline constexpr int WIFI_PS_MIN_MODEM = 1;
using wifi_second_chan_t = int;
inline constexpr int WIFI_SECOND_CHAN_NONE = 0;
struct wifi_ap_record_t { uint8_t primary; };
int esp_wifi_get_channel(uint8_t *, wifi_second_chan_t *);
int esp_wifi_set_channel(uint8_t, wifi_second_chan_t);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
