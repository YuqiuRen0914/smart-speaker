#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
void lunar_espnow_poll(void);
bool lunar_espnow_has_resources(void);
// Network owner only. Management frames are admitted from exactly this MAC;
// DATA remains on its separate product queue. No persistence or task creation.
esp_err_t lunar_espnow_management_open(const uint8_t peer[6]);
// Initiator admission accepts only the corresponding reply families, with
// the same exact peer gate and RAM-only key lifetime as a responder.
esp_err_t lunar_espnow_management_open_client(const uint8_t peer[6]);
esp_err_t lunar_espnow_management_set_key(const uint8_t key[32]);
esp_err_t lunar_espnow_management_close(void);
// Explicit RAM-only native group. Remote GROUP membership commands remain
// disabled; group allocation and CCM retirement run on Network only.
esp_err_t lunar_espnow_group_open(const uint8_t group[6],const uint8_t transmitter[6],const uint8_t key[32]);
esp_err_t lunar_espnow_group_close(void);
#ifdef __cplusplus
}
#endif
