#pragma once
#include <cstdint>
#include <string>
#include "model/operation_outcome.h"
namespace lunaredu::lunaros {
// Wire-neutral completion value; transport adapters decide how to encode it.
struct DeviceOperationResult {
  bool ok = true;
  uint16_t status_code = 200;
  std::string error;
  // Legacy three-field failures cannot prove that an effect never started.
  // Keep aggregate initialization source-compatible, conservatively unknown.
  OperationCompletion completion=ok?OperationCompletion::Completed:OperationCompletion::Unknown;
  OperationError category=ok?OperationError::None:OperationError::Internal;
};
}
