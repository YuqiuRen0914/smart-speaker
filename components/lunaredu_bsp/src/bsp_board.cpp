#include "bsp/lunaredu_bsp.h"

namespace {

const bsp_board_info_t kBoardInfo = {
    .id = lunaredu::bsp::pins::kBoardId,
    .name = lunaredu::bsp::pins::kBoardName,
    .board_type = lunaredu::bsp::pins::kBoardType,
    .model = lunaredu::bsp::pins::kBoardModel,
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
