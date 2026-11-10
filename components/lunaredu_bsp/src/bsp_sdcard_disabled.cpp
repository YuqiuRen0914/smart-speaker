#include "bsp/lunaredu_bsp.h"
esp_err_t bsp_sdcard_mount(const char *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bsp_sdcard_unmount() { return ESP_OK; }
bsp_sdcard_status_t bsp_sdcard_get_status() {
  return {false, "/sdcard", 0, "sdcard_not_available_on_board"};
}
