#include "persistence/system/board_support.h"

#include <esp_err.h>

#include "bsp/lunaredu_bsp.h"

namespace lunaredu::lunaros {

bool board_support_initialize(std::string &error) {
  const esp_err_t result = bsp_board_init();
  if (result == ESP_OK) {
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
  return false;
}

BoardSupportFacts board_support_facts() {
  BoardSupportFacts facts;
  const bsp_board_info_t *board = bsp_board_get_info();
  if (board != nullptr) {
    facts.id = board->id != nullptr ? board->id : "";
    facts.type = board->board_type != nullptr ? board->board_type : "";
    facts.model = board->model != nullptr ? board->model : "";
  }
  return facts;
}

} // namespace lunaredu::lunaros
