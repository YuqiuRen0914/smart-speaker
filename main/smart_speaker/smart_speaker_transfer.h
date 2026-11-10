#pragma once
#include <cstddef>
#include <string>
namespace smart_speaker {
struct TransferStatus {
  bool receiving=false;
  std::string name,error;
  size_t received=0,total=0;
};
TransferStatus transfer_snapshot();
}
