#pragma once
#include <string>
#include <cstddef>
#include <cstdint>
namespace lunaredu::lunaros {
struct BoardSupportFacts {
  std::string id;
  std::string type;
  std::string model;
  std::string hardware_revision;
  std::string support_status;
};
BoardSupportFacts board_support_facts();
struct BoardResourceFacts {
  const char *name = nullptr;
  bool configured_available = false;
  uint64_t pin_mask = 0;
  char owner[32] = {};
  struct Signal { const char *role = nullptr; int gpio = -1; };
  Signal signals[8] = {};
  size_t signal_count = 0;
};
size_t board_resource_count();
bool board_resource_facts(size_t index, BoardResourceFacts &out);
} // namespace lunaredu::lunaros
