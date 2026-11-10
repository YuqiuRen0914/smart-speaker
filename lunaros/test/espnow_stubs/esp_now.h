#pragma once
#include <cstddef>
#include "esp_wifi.h"
using esp_now_send_status_t = int;
struct esp_now_recv_info_t { const uint8_t *src_addr; const wifi_pkt_rx_ctrl_t *rx_ctrl; };
struct esp_now_peer_info_t { uint8_t peer_addr[6]; uint8_t channel; int ifidx; bool encrypt; };
using esp_now_recv_cb_t = void (*)(const esp_now_recv_info_t *, const uint8_t *, int);
int esp_now_init();
int esp_now_deinit();
int esp_now_register_send_cb(void (*)(const wifi_tx_info_t *, esp_now_send_status_t));
int esp_now_register_recv_cb(esp_now_recv_cb_t);
int esp_now_unregister_send_cb();
int esp_now_unregister_recv_cb();
bool esp_now_is_peer_exist(const uint8_t *);
int esp_now_add_peer(const esp_now_peer_info_t *);
int esp_now_mod_peer(const esp_now_peer_info_t *);
int esp_now_get_peer(const uint8_t *, esp_now_peer_info_t *);
int esp_now_del_peer(const uint8_t *);
int esp_now_send(const uint8_t *, const uint8_t *, size_t);
