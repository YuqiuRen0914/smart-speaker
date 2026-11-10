#include "bsp/lunaros_board_facts.h"

#include <cstring>

#include "bsp/lunaredu_bsp.h"

namespace lunaredu::lunaros {

BoardSupportFacts board_support_facts() {
  BoardSupportFacts facts;
  const bsp_board_info_t *board = bsp_board_get_info();
  if (board != nullptr) {
    facts.id = board->id != nullptr ? board->id : "";
    facts.type = board->board_type != nullptr ? board->board_type : "";
    facts.model = board->model != nullptr ? board->model : "";
    facts.hardware_revision = board->hardware_revision != nullptr ? board->hardware_revision : "unknown";
  }
  facts.support_status = bsp_board_support_status();
  return facts;
}
size_t board_resource_count() { return BSP_RESOURCE_COUNT; }
bool board_resource_facts(size_t index, BoardResourceFacts &out) {
  if (index >= BSP_RESOURCE_COUNT) return false;
  bsp_resource_observation_t value{};
  if (!bsp_resource_observe(static_cast<bsp_resource_t>(index), &value)) return false;
  out = {};
  out.name = value.name;
  out.configured_available = value.configured_available;
  out.pin_mask = value.pin_mask;
  static_assert(sizeof(out.owner) == sizeof(value.owner));
  std::memcpy(out.owner, value.owner, sizeof(out.owner));
  bsp_signal_observation_t signals[8] = {};
  out.signal_count = bsp_resource_signals(static_cast<bsp_resource_t>(index), signals, 8);
  for (size_t i = 0; i < out.signal_count && i < 8; ++i)
    out.signals[i] = {signals[i].role, signals[i].gpio};
  return true;
}

} // namespace lunaredu::lunaros
