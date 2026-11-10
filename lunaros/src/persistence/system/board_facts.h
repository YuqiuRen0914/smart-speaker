#pragma once
#include <string>
namespace lunaredu::lunaros {
struct BoardSupportFacts {
  std::string id;
  std::string type;
  std::string model;
};
BoardSupportFacts board_support_facts();
} // namespace lunaredu::lunaros
