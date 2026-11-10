#include "bsp/lunaredu_bsp.h"
#include "sdkconfig.h"
#ifndef CONFIG_LUNAROS_BOARD_ID
#define CONFIG_LUNAROS_BOARD_ID "unknown"
#endif
#ifndef CONFIG_LUNAROS_BOARD_NAME
#define CONFIG_LUNAROS_BOARD_NAME "unknown"
#endif
#ifndef CONFIG_LUNAROS_BOARD_TYPE
#define CONFIG_LUNAROS_BOARD_TYPE "unknown"
#endif
#ifndef CONFIG_LUNAROS_BOARD_MODEL
#define CONFIG_LUNAROS_BOARD_MODEL "unknown"
#endif
#ifndef CONFIG_LUNAROS_BOARD_REVISION
#define CONFIG_LUNAROS_BOARD_REVISION "unknown"
#endif


namespace {

const bsp_board_info_t kBoardInfo = {
    .id = CONFIG_LUNAROS_BOARD_ID,
    .name = CONFIG_LUNAROS_BOARD_NAME,
    .board_type = CONFIG_LUNAROS_BOARD_TYPE,
    .model = CONFIG_LUNAROS_BOARD_MODEL,
    .hardware_revision = CONFIG_LUNAROS_BOARD_REVISION,
};

bool g_board_initialized = false;

} // 匿名命名空间

esp_err_t bsp_board_init() {
  if (g_board_initialized) {
    return ESP_OK;
  }
  bsp_resource_reset_all();
  g_board_initialized = true;
  return ESP_OK;
}

const bsp_board_info_t *bsp_board_get_info() { return &kBoardInfo; }
