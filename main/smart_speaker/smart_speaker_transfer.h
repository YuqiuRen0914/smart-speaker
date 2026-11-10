#pragma once
#include <cstdint>
#include <string>
namespace smart_speaker {
struct TransferStatus {
  uint32_t generation = 0;
  bool receiving = false;
  bool batch_open = false;
  std::string batch_id;
  std::string name;
  std::string error;
  uint32_t received = 0;
  uint32_t total = 0;
  uint32_t completed = 0;
  uint32_t failed = 0;
  uint32_t updated_ms = 0;
};
TransferStatus transfer_snapshot();
bool dismiss_transfer();
bool transfer_dismissed(uint32_t generation);
}
